/*
 * 加热闭环控制模块：
 * 1. TIM2_CH1/PA0 产生 1 kHz PID PWM，接 UCC27517 的 IN+；
 * 2. PC2 产生低脉冲，复位 74LVC1G74 的低有效硬件故障锁存；
 * 3. PC3 监视 74LVC1G74 的 Q#，下降沿仅通知任务，任务负责安全停机和报警；
 * 4. ADC1 DMA 提供板温和液体温度，PID 只在整机 RUNNING 且无故障时输出。
 */
#include "App_heater.h"
#include "App_buzzer.h"
#include "App_system.h"
#include "App_ui.h"
#include "Com_debug.h"
#include "Dri_adc.h"
#include "bsp_pins.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>

#define APP_HEATER_TASK_PERIOD_MS        500U
#define APP_HEATER_DEBUG_PERIOD_MS       2000U
#define APP_HEATER_FAULT_RESET_MS        2U
#define APP_HEATER_ADC_RAIL_LOW          5U
#define APP_HEATER_ADC_RAIL_HIGH         4090U
#define APP_HEATER_ADC_MAX               4095.0f
#define APP_HEATER_NTC_R0_OHM            10000.0f
#define APP_HEATER_NTC_PULLUP_OHM        10000.0f
#define APP_HEATER_NTC_B                 4950.0f
#define APP_HEATER_NTC_T0_K              298.15f
#define APP_HEATER_LIQUID_OVERTEMP_C     130.0f
#define APP_HEATER_BOARD_OVERTEMP_C      110.0f
#define APP_HEATER_PID_KP                0.030f
#define APP_HEATER_PID_KI                0.002f
#define APP_HEATER_PID_KD                0.000f
#define APP_HEATER_PID_INTEGRAL_LIMIT    300.0f
#define APP_HEATER_PWM_MAX_DUTY          0.60f
#define APP_HEATER_BOARD_TEMP_GAIN       1.80f
#define APP_HEATER_BOARD_DERATE_START_C  75.0f
#define APP_HEATER_BOARD_DERATE_STOP_C   95.0f
#define APP_HEATER_BOOT_CAL_SAMPLES      10U
#define APP_HEATER_BOOT_CAL_INTERVAL_MS  100U
#define APP_HEATER_BOOT_CAL_MAX_DIFF_C   8.0f

typedef struct
{
    float integral;
    float previousError;
    float lastDuty;
} AppHeaterPid_t;

static AppHeaterPid_t s_heaterPid;
static TaskHandle_t s_heaterTaskHandle;
static uint8_t s_pwmStarted;
static uint8_t s_faultReported;
static float s_boardTemperatureBase;
static float s_boardTemperatureOffset;
static float s_liquidTemperatureOffset;
static uint8_t s_coldCalibrationReady;

static void App_HeaterStopOutput(void)
{
    /* 先清比较值再停止通道，避免 PA0 停止时残留有效电平。 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
    (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    /* MOTOR_ON 是风扇开关；加热停止时同步关闭，满足 IDLE/FAULT 安全状态。 */
    HAL_GPIO_WritePin(MOTOR_ON_GPIO_Port, MOTOR_ON_Pin,
                      MOTOR_ON_INACTIVE_STATE);
    s_pwmStarted = 0U;
    s_heaterPid.lastDuty = 0.0f;
}

static void App_HeaterResetPid(void)
{
    s_heaterPid.integral = 0.0f;
    s_heaterPid.previousError = 0.0f;
    s_heaterPid.lastDuty = 0.0f;
}

static uint8_t App_HeaterNtcValid(uint16_t raw)
{
    return ((raw > APP_HEATER_ADC_RAIL_LOW) &&
            (raw < APP_HEATER_ADC_RAIL_HIGH)) ? 1U : 0U;
}

static float App_HeaterNtcToTemperature(uint16_t raw)
{
    float resistance;
    float inverseTemperature;

    resistance = APP_HEATER_NTC_PULLUP_OHM * (float)raw /
                 (APP_HEATER_ADC_MAX - (float)raw);
    inverseTemperature = (1.0f / APP_HEATER_NTC_T0_K) +
                         (logf(resistance / APP_HEATER_NTC_R0_OHM) /
                          APP_HEATER_NTC_B);
    return (1.0f / inverseTemperature) - 273.15f;
}

static float App_HeaterAbs(float value)
{
    return (value < 0.0f) ? -value : value;
}

/*
 * 冷机自校准：上电时两颗 NTC 应处于同一环境温度，取多次平均消除噪声，
 * 再把两路读数拉到共同参考温度。校准失败时保留理论 Beta 模型结果。
 */
static void App_HeaterColdCalibrate(void)
{
    DriAdcSample_t sample;
    float boardSum = 0.0f;
    float liquidSum = 0.0f;
    float boardAverage;
    float liquidAverage;
    float referenceTemperature;
    uint8_t validCount = 0U;
    uint8_t index;

    s_boardTemperatureBase = 0.0f;
    s_boardTemperatureOffset = 0.0f;
    s_liquidTemperatureOffset = 0.0f;
    s_coldCalibrationReady = 0U;

    for (index = 0U; index < APP_HEATER_BOOT_CAL_SAMPLES; index++)
    {
        if ((Dri_AdcReadAll(&sample) != 0U) &&
            (App_HeaterNtcValid(sample.boardNtc) != 0U) &&
            (App_HeaterNtcValid(sample.liquidNtc) != 0U))
        {
            boardSum += App_HeaterNtcToTemperature(sample.boardNtc);
            liquidSum += App_HeaterNtcToTemperature(sample.liquidNtc);
            validCount++;
        }
        vTaskDelay(pdMS_TO_TICKS(APP_HEATER_BOOT_CAL_INTERVAL_MS));
    }

    if (validCount < 3U)
    {
        Com_DebugPrintf("HEAT cold calibration unavailable\r\n");
        return;
    }

    boardAverage = boardSum / (float)validCount;
    liquidAverage = liquidSum / (float)validCount;
    if (App_HeaterAbs(boardAverage - liquidAverage) >
        APP_HEATER_BOOT_CAL_MAX_DIFF_C)
    {
        Com_DebugPrintf("HEAT cold calibration skipped board:%d liquid:%d\r\n",
                        (int)boardAverage, (int)liquidAverage);
        return;
    }

    referenceTemperature = (boardAverage + liquidAverage) * 0.5f;
    s_boardTemperatureOffset = referenceTemperature - boardAverage;
    s_liquidTemperatureOffset = referenceTemperature - liquidAverage;
    s_boardTemperatureBase = referenceTemperature;
    s_coldCalibrationReady = 1U;

    Com_DebugPrintf("HEAT cold calibration ok ref:%d boardOfs:%d liquidOfs:%d\r\n",
                    (int)referenceTemperature,
                    (int)s_boardTemperatureOffset,
                    (int)s_liquidTemperatureOffset);
}

/* 板温超过基准后放大升温部分，使控制器提前降低加热功率。 */
static float App_HeaterCompensateBoardTemperature(float boardTemperature)
{
    if ((s_coldCalibrationReady != 0U) &&
        (boardTemperature > s_boardTemperatureBase))
    {
        return s_boardTemperatureBase +
               ((boardTemperature - s_boardTemperatureBase) *
                APP_HEATER_BOARD_TEMP_GAIN);
    }

    return boardTemperature;
}

/* 板温 75~95°C 线性降额，达到 95°C 时完全关闭加热输出。 */
static float App_HeaterLimitDutyByBoard(float duty, float boardTemperature)
{
    float scale;

    if (boardTemperature >= APP_HEATER_BOARD_DERATE_STOP_C)
    {
        return 0.0f;
    }

    if (boardTemperature > APP_HEATER_BOARD_DERATE_START_C)
    {
        scale = (APP_HEATER_BOARD_DERATE_STOP_C - boardTemperature) /
                (APP_HEATER_BOARD_DERATE_STOP_C - APP_HEATER_BOARD_DERATE_START_C);
        duty *= scale;
    }

    return duty;
}

static float App_HeaterPidStep(float targetTemperature, float currentTemperature)
{
    float error;
    float derivative;
    float duty;
    const float dt = (float)APP_HEATER_TASK_PERIOD_MS / 1000.0f;

    error = targetTemperature - currentTemperature;
    if (error <= 0.0f)
    {
        /* 加热器只能升温，达到目标后清除积分，防止下一次启动发生积分冲击。 */
        App_HeaterResetPid();
        return 0.0f;
    }

    s_heaterPid.integral += error * dt;
    if (s_heaterPid.integral > APP_HEATER_PID_INTEGRAL_LIMIT)
    {
        s_heaterPid.integral = APP_HEATER_PID_INTEGRAL_LIMIT;
    }
    derivative = (error - s_heaterPid.previousError) / dt;
    duty = (APP_HEATER_PID_KP * error) +
           (APP_HEATER_PID_KI * s_heaterPid.integral) +
           (APP_HEATER_PID_KD * derivative);
    s_heaterPid.previousError = error;

    if (duty > APP_HEATER_PWM_MAX_DUTY)
    {
        duty = APP_HEATER_PWM_MAX_DUTY;
    }
    return duty;
}

static void App_HeaterApplyDuty(float duty)
{
    uint32_t period;
    uint32_t compare;

    if (duty <= 0.0f)
    {
        App_HeaterStopOutput();
        return;
    }

    /* 只有真正需要加热时才开启散热风扇。 */
    HAL_GPIO_WritePin(MOTOR_ON_GPIO_Port, MOTOR_ON_Pin,
                      MOTOR_ON_ACTIVE_STATE);
    period = __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;
    compare = (uint32_t)(duty * (float)period);
    if (compare >= period)
    {
        compare = period - 1U;
    }
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, compare);
    if (s_pwmStarted == 0U)
    {
        if (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK)
        {
            __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
            /* PWM 启动失败时回收风扇输出，避免出现无加热却开风扇。 */
            HAL_GPIO_WritePin(MOTOR_ON_GPIO_Port, MOTOR_ON_Pin,
                              MOTOR_ON_INACTIVE_STATE);
            return;
        }
        s_pwmStarted = 1U;
    }
    s_heaterPid.lastDuty = duty;
}

static uint8_t App_HeaterApplyDutyIfAllowed(float duty)
{
    uint8_t allowed = 0U;

    /* 最终输出闸门：状态复核与 PWM 写入放在同一互斥区。 */
    if (App_SystemLock(portMAX_DELAY) != 0U)
    {
        if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
            (g_appData.FaultFlags == APP_FAULT_NONE) &&
            (g_appData.TargetTemperature > 0))
        {
            App_HeaterApplyDuty(duty);
            allowed = 1U;
        }
        App_SystemUnlock();
    }
    return allowed;
}

static void App_HeaterEnterFault(AppFault_t fault, const char *message)
{
    /* 故障处理顺序固定为：撤销本模块输出、整机安全停机、报警、刷新界面。 */
    App_HeaterStopOutput();
    (void)App_SystemSetFault(fault);
    App_BuzzerSetContinuous(1U);
    if (s_faultReported == 0U)
    {
        s_faultReported = 1U;
        debug_printfln("%s", message);
    }
    App_UiNotify();
}

static void App_HeaterTask(void *argument)
{
    DriAdcSample_t sample;
    AppMotorStatusValue_t status;
    float boardTemperature;
    float liquidTemperature;
    float targetTemperature;
    float duty;
    TickType_t lastDebugTick;
    uint8_t adcFailureCount = 0U;

    (void)argument;
    App_HeaterColdCalibrate();
    lastDebugTick = xTaskGetTickCount();

    for (;;)
    {
        /* PC3 中断可提前唤醒；无中断时每 500 ms 至少完成一次温控与安全检查。 */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(APP_HEATER_TASK_PERIOD_MS));

        if (HAL_GPIO_ReadPin(HEAT_FAULT_N_GPIO_Port, HEAT_FAULT_N_Pin) ==
            HEAT_FAULT_ACTIVE_STATE)
        {
            App_HeaterEnterFault(APP_FAULT_HEATER_ELECTRICAL,
                                 "Heater hardware fault");
            continue;
        }

        if (Dri_AdcReadAll(&sample) == 0U)
        {
            adcFailureCount++;
            if (adcFailureCount >= 3U)
            {
                App_HeaterEnterFault(APP_FAULT_ADC_RUNTIME,
                                     "Heater ADC unavailable");
            }
            continue;
        }
        adcFailureCount = 0U;

        if ((App_HeaterNtcValid(sample.boardNtc) == 0U) ||
            (App_HeaterNtcValid(sample.liquidNtc) == 0U))
        {
            App_HeaterEnterFault(APP_FAULT_HEATER_SENSOR,
                                 "Heater NTC open or short");
            continue;
        }

        boardTemperature = App_HeaterNtcToTemperature(sample.boardNtc);
        liquidTemperature = App_HeaterNtcToTemperature(sample.liquidNtc);
        boardTemperature += s_boardTemperatureOffset;
        liquidTemperature += s_liquidTemperatureOffset;
        boardTemperature = App_HeaterCompensateBoardTemperature(boardTemperature);

        /* 把板温和液温一起写入共享状态，UI 任务只读取快照，不持锁访问 OLED。 */
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            g_appData.CurrentBoardTemperature =
                (int16_t)(boardTemperature + 0.5f);
            g_appData.CurrentTemperature =
                (int16_t)(liquidTemperature + 0.5f);
            App_SystemUnlock();
        }
        App_UiNotify();

        if ((boardTemperature >= APP_HEATER_BOARD_OVERTEMP_C) ||
            (liquidTemperature >= APP_HEATER_LIQUID_OVERTEMP_C))
        {
            App_HeaterEnterFault(APP_FAULT_HEATER_OVERTEMP,
                                 "Heater overtemperature");
            continue;
        }

        status = APP_MOTOR_STATUS_IDLE;
        targetTemperature = 0.0f;
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            status = g_appData.CurrentStatus;
            targetTemperature = (float)g_appData.TargetTemperature;
            App_SystemUnlock();
        }
        App_UiNotify();

        if ((status == APP_MOTOR_STATUS_RUNNING) &&
            (targetTemperature > 0.0f))
        {
            duty = App_HeaterPidStep(targetTemperature, liquidTemperature);
            duty = App_HeaterLimitDutyByBoard(duty, boardTemperature);
            if (App_HeaterApplyDutyIfAllowed(duty) == 0U)
            {
                App_HeaterResetPid();
                App_HeaterStopOutput();
            }
        }
        else
        {
            App_HeaterResetPid();
            App_HeaterStopOutput();
        }

        if ((xTaskGetTickCount() - lastDebugTick) >=
            pdMS_TO_TICKS(APP_HEATER_DEBUG_PERIOD_MS))
        {
            lastDebugTick = xTaskGetTickCount();
            Com_DebugPrintf("HEAT target:%d liquid:%d board:%d duty:%u%% raw:%u/%u\r\n",
                            (int)targetTemperature, (int)liquidTemperature,
                            (int)boardTemperature,
                            (unsigned int)(s_heaterPid.lastDuty * 100.0f),
                            sample.liquidNtc, sample.boardNtc);
        }
    }
}

uint8_t App_HeaterInit(void)
{
    App_HeaterStopOutput();
    App_HeaterResetPid();
    s_faultReported = 0U;
    s_boardTemperatureBase = 0.0f;
    s_boardTemperatureOffset = 0.0f;
    s_liquidTemperatureOffset = 0.0f;
    s_coldCalibrationReady = 0U;

    /*
     * PC2 使用开漏输出，板上 R36 将其上拉到 3.3 V。低脉冲复位锁存器后，
     * Q 恢复低电平，使 UCC27517 的 IN- 解除硬件封锁；正常通断仍由 PA0 控制。
     */
    HAL_GPIO_WritePin(FAULT_RST_N_GPIO_Port, FAULT_RST_N_Pin,
                      FAULT_RST_ASSERT_STATE);
    HAL_Delay(APP_HEATER_FAULT_RESET_MS);
    HAL_GPIO_WritePin(FAULT_RST_N_GPIO_Port, FAULT_RST_N_Pin,
                      FAULT_RST_RELEASE_STATE);
    return 1U;
}

uint8_t App_HeaterCreateTask(void)
{
    return (xTaskCreate(App_HeaterTask, "Heater", 384U, NULL,
                        tskIDLE_PRIORITY + 2U,
                        &s_heaterTaskHandle) == pdPASS) ? 1U : 0U;
}

void App_HeaterExtiCallback(uint16_t gpioPin)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    if ((gpioPin == HEAT_FAULT_N_Pin) && (s_heaterTaskHandle != NULL))
    {
        /* 不在中断中操作 PWM、蜂鸣器、互斥锁或共享状态。 */
        vTaskNotifyGiveFromISR(s_heaterTaskHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

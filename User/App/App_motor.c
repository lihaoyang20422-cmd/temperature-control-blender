#include "App_motor.h"
#include "App_system.h"
#include "App_ui.h"
#include "Com_debug.h"
#include "bsp_pins.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"

/* 电机控制周期、编码器参数和前馈参数。 */
#define APP_MOTOR_CONTROL_PERIOD_MS       50U
#define APP_MOTOR_DEBUG_PERIOD_MS         1000U
#define APP_MOTOR_ENCODER_COUNTS_PER_REV  12.5f
#define APP_MOTOR_SPEED_AVG_SAMPLES       8U
#define APP_MOTOR_FF_DUTY_PER_RPM         0.00143f
#define APP_MOTOR_PID_KP                  8.0f
#define APP_MOTOR_PID_KI                  0.8f
#define APP_MOTOR_PID_KD                  0.0f
#define APP_MOTOR_PID_OUTPUT_SCALE        10000.0f
#define APP_MOTOR_PID_INTEGRAL_LIMIT      800.0f
#define APP_MOTOR_FF_MAX_DUTY             0.90f  /* 前馈最多占 90%。 */
#define APP_MOTOR_PID_MAX_CORRECTION      0.10f  /* PID 修正最多占 10%。 */
#define APP_MOTOR_PWM_MAX_DUTY            1.00f  /* 前馈与 PID 合计允许达到 100%。 */
#define APP_MOTOR_PWM_START_DUTY          0.08f

typedef struct
{
    float integral;
    float previousError;
} AppMotorPid_t;

static AppMotorPid_t s_motorPid;
static int32_t s_lastEncoderCount;
static int32_t s_speedAccumCount;
static uint8_t s_speedSampleCount;
static float s_filteredRpm;
static float s_lastDuty;
static uint8_t s_pwmStarted;

static int32_t App_MotorReadEncoder(void)
{
    return (int32_t)__HAL_TIM_GET_COUNTER(&htim3);
}

static void App_MotorResetController(void)
{
    s_motorPid.integral = 0.0f;
    s_motorPid.previousError = 0.0f;
    s_speedAccumCount = 0;
    s_speedSampleCount = 0U;
    s_filteredRpm = 0.0f;
    s_lastDuty = 0.0f;
}

static float App_MotorUpdateRpm(int32_t deltaCount)
{
    float sampleMinutes;

    s_speedAccumCount += deltaCount;
    s_speedSampleCount++;

    if (s_speedSampleCount >= APP_MOTOR_SPEED_AVG_SAMPLES)
    {
        sampleMinutes = ((float)APP_MOTOR_CONTROL_PERIOD_MS *
                         (float)APP_MOTOR_SPEED_AVG_SAMPLES) / 60000.0f;
        s_filteredRpm = ((float)s_speedAccumCount /
                         APP_MOTOR_ENCODER_COUNTS_PER_REV) / sampleMinutes;
        if (s_filteredRpm < 0.0f)
        {
            s_filteredRpm = -s_filteredRpm;
        }
        s_speedAccumCount = 0;
        s_speedSampleCount = 0U;
    }

    return s_filteredRpm;
}

static float App_MotorPidStep(float targetRpm, float currentRpm)
{
    float error;
    float derivative;
    float correction;
    float output;

    error = targetRpm - currentRpm;
    s_motorPid.integral += error * ((float)APP_MOTOR_CONTROL_PERIOD_MS / 1000.0f);
    if (s_motorPid.integral > APP_MOTOR_PID_INTEGRAL_LIMIT)
    {
        s_motorPid.integral = APP_MOTOR_PID_INTEGRAL_LIMIT;
    }
    else if (s_motorPid.integral < -APP_MOTOR_PID_INTEGRAL_LIMIT)
    {
        s_motorPid.integral = -APP_MOTOR_PID_INTEGRAL_LIMIT;
    }

    derivative = (error - s_motorPid.previousError) /
                 ((float)APP_MOTOR_CONTROL_PERIOD_MS / 1000.0f);
    correction = (APP_MOTOR_PID_KP * error +
                  APP_MOTOR_PID_KI * s_motorPid.integral +
                  APP_MOTOR_PID_KD * derivative) / APP_MOTOR_PID_OUTPUT_SCALE;
    /*
     * 前馈负责主要输出，按标定曲线最多提供 90% 占空比；PID 只修正
     * 负载扰动和模型误差，修正量限制为 +/-10%，避免积分或突发误差
     * 把输出直接推到不可控的满量程。
     */
    output = targetRpm * APP_MOTOR_FF_DUTY_PER_RPM;
    if (output > APP_MOTOR_FF_MAX_DUTY)
    {
        output = APP_MOTOR_FF_MAX_DUTY;
    }

    if (correction > APP_MOTOR_PID_MAX_CORRECTION)
    {
        correction = APP_MOTOR_PID_MAX_CORRECTION;
    }
    else if (correction < -APP_MOTOR_PID_MAX_CORRECTION)
    {
        correction = -APP_MOTOR_PID_MAX_CORRECTION;
    }
    output += correction;
    s_motorPid.previousError = error;

    if (output < 0.0f)
    {
        output = 0.0f;
    }
    else if (output > APP_MOTOR_PWM_MAX_DUTY)
    {
        output = APP_MOTOR_PWM_MAX_DUTY;
    }

    return output;
}

static void App_MotorApplyDuty(float duty)
{
    uint32_t period;
    uint32_t pulse;

    if (duty <= 0.0f)
    {
        App_MotorStop();
        return;
    }
    if (duty < APP_MOTOR_PWM_START_DUTY)
    {
        duty = APP_MOTOR_PWM_START_DUTY;
    }
    if (duty > APP_MOTOR_PWM_MAX_DUTY)
    {
        duty = APP_MOTOR_PWM_MAX_DUTY;
    }

    period = (uint32_t)__HAL_TIM_GET_AUTORELOAD(&htim4) + 1U;
    pulse = (uint32_t)(duty * (float)period);
    if (pulse >= period)
    {
        pulse = period - 1U;
    }

    HAL_GPIO_WritePin(MOTOR2_GPIO_Port, MOTOR2_Pin, MOTOR2_FIXED_STATE);
    if (s_pwmStarted == 0U)
    {
        (void)HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
        s_pwmStarted = 1U;
    }
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, pulse);
    s_lastDuty = duty;
}

static uint8_t App_MotorApplyDutyIfRunning(float duty)
{
    uint8_t allowed = 0U;

    /* 锁内再次确认状态，避免状态切换后仍输出旧的 PWM。 */
    if (App_SystemLock(portMAX_DELAY) != 0U)
    {
        if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
            (g_appData.TargetSpeed > 0))
        {
            App_MotorApplyDuty(duty);
            allowed = 1U;
        }
        App_SystemUnlock();
    }

    return allowed;
}

static void App_MotorUpdateCurrentSpeed(int16_t speed)
{
    if (App_SystemLock(portMAX_DELAY) != 0U)
    {
        if (g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING)
        {
            g_appData.CurrentSpeed = speed;
        }
        else
        {
            g_appData.CurrentSpeed = 0;
        }
        App_SystemUnlock();
    }
    App_UiNotify();
}

static void App_MotorTask(void *argument)
{
    int32_t encoderCount;
    int32_t deltaCount;
    float currentRpm;
    float targetRpm;
    float duty;
    int16_t speedToStore;
    AppMotorStatusValue_t status;
    TickType_t lastDebugTick;

    (void)argument;
    encoderCount = App_MotorReadEncoder();
    s_lastEncoderCount = encoderCount;
    App_MotorResetController();
    lastDebugTick = xTaskGetTickCount();

    for (;;)
    {
        status = APP_MOTOR_STATUS_IDLE;
        targetRpm = 0.0f;
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            status = g_appData.CurrentStatus;
            targetRpm = (float)g_appData.TargetSpeed;
            App_SystemUnlock();
        }

        encoderCount = App_MotorReadEncoder();
        deltaCount = (int16_t)(encoderCount - s_lastEncoderCount);
        s_lastEncoderCount = encoderCount;
        currentRpm = App_MotorUpdateRpm(deltaCount);

        if ((status == APP_MOTOR_STATUS_RUNNING) && (targetRpm > 0.0f))
        {
            duty = App_MotorPidStep(targetRpm, currentRpm);
            if (App_MotorApplyDutyIfRunning(duty) == 0U)
            {
                App_MotorResetController();
                App_MotorStop();
            }
        }
        else
        {
            App_MotorResetController();
            App_MotorStop();
        }

        speedToStore = (int16_t)(currentRpm + 0.5f);
        if ((status != APP_MOTOR_STATUS_RUNNING) || (targetRpm <= 0.0f))
        {
            speedToStore = 0;
        }
        App_MotorUpdateCurrentSpeed(speedToStore);

        if ((status == APP_MOTOR_STATUS_RUNNING) &&
            ((xTaskGetTickCount() - lastDebugTick) >=
             pdMS_TO_TICKS(APP_MOTOR_DEBUG_PERIOD_MS)))
        {
            lastDebugTick = xTaskGetTickCount();
            Com_DebugPrintf("MOTOR target:%d current:%d duty:%u%% delta:%ld\r\n",
                            (int)targetRpm,
                            (int)speedToStore,
                            (unsigned int)(s_lastDuty * 100.0f),
                            (long)deltaCount);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_MOTOR_CONTROL_PERIOD_MS));
    }
}

uint8_t App_MotorInit(void)
{
    if (HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL) != HAL_OK)
    {
        return 0U;
    }

    __HAL_TIM_SET_COUNTER(&htim3, 0U);
    s_lastEncoderCount = 0;
    s_pwmStarted = 0U;
    App_MotorResetController();
    App_MotorStop();
    return 1U;
}

uint8_t App_MotorSetPwm(uint16_t compare)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim4);

    if ((uint32_t)compare > period)
    {
        compare = (uint16_t)period;
    }
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, compare);
    return 1U;
}

uint8_t App_MotorStartPwm(void)
{
    HAL_GPIO_WritePin(MOTOR2_GPIO_Port, MOTOR2_Pin, MOTOR2_FIXED_STATE);
    if (HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3) != HAL_OK)
    {
        return 0U;
    }
    s_pwmStarted = 1U;
    return 1U;
}

void App_MotorStop(void)
{
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3);
    HAL_GPIO_WritePin(MOTOR2_GPIO_Port, MOTOR2_Pin, MOTOR2_FIXED_STATE);
    s_pwmStarted = 0U;
    s_lastDuty = 0.0f;
}

void App_MotorCreateTask(void)
{
    (void)xTaskCreate(App_MotorTask, "Motor", 320U, NULL,
                      tskIDLE_PRIORITY + 2U, NULL);
}

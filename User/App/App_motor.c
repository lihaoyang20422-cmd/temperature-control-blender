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
#define APP_MOTOR_VOFA_PERIOD_MS          100U
#define APP_MOTOR_DEBUG_PERIOD_MS         1000U
#define APP_MOTOR_LOG_ENABLE              1U
#define APP_MOTOR_ENCODER_COUNTS_PER_REV  8.0f
#define APP_MOTOR_SPEED_AVG_SAMPLES       8U
#define APP_MOTOR_STOP_SPEED_TIMEOUT_MS   500U
/*
 * 电机存在明显的低占空比死区，前馈不能使用经过原点的单斜率模型。
 * 根据实测约 140 RPM/38% 和 500 RPM/48% 两个工作点，拟合得到：
 * duty = 0.34 + 0.00028 * targetRpm。PID 仅修正负载扰动和模型余差。
 */
#define APP_MOTOR_FF_BASE_DUTY            0.34f
#define APP_MOTOR_FF_DUTY_PER_RPM         0.00028f
#define APP_MOTOR_PID_KP                  8.0f
#define APP_MOTOR_PID_KI                  0.8f
#define APP_MOTOR_PID_KD                  0.0f
#define APP_MOTOR_PID_OUTPUT_SCALE        10000.0f
#define APP_MOTOR_PID_INTEGRAL_LIMIT      800.0f
#define APP_MOTOR_FF_MAX_DUTY             0.90f  /* 前馈最多占 90%。 */
#define APP_MOTOR_PID_MAX_CORRECTION      0.10f  /* PID 修正最多占 10%。 */
#define APP_MOTOR_PWM_MAX_DUTY            1.00f  /* 前馈与 PID 合计允许达到 100%。 */
#define APP_MOTOR_PWM_START_DUTY          0.08f
#define APP_MOTOR_STARTUP_BOOST_DUTY      0.70f
#define APP_MOTOR_STARTUP_BOOST_TIME_MS   300U
#define APP_MOTOR_STARTUP_EXIT_RATIO      0.80f
#define APP_MOTOR_SPEED_LIMIT_RELEASE_RPM 950.0f
/* 触发限速后保留不高于约 1000 RPM 所需的前馈占空比，避免突然全断造成机械冲击。 */
#define APP_MOTOR_SPEED_LIMIT_DUTY        0.62f

typedef struct
{
    float integral;
    float previousError;
} AppMotorPid_t;

static AppMotorPid_t s_motorPid;
static int32_t s_lastEncoderCount;
static int32_t s_speedAccumCount;
static int32_t s_speedDeltaWindow[APP_MOTOR_SPEED_AVG_SAMPLES];
static uint8_t s_speedSampleCount;
static uint8_t s_speedSampleIndex;
static float s_filteredRpm;
static float s_lastDuty;
static uint8_t s_pwmStarted;
static uint16_t s_startupBoostRemainingMs;
/* 停止后仍保留编码器速度显示，连续无脉冲达到超时才清零。 */
static uint16_t s_stopNoPulseElapsedMs;
static uint8_t s_speedLimitActive;

static int32_t App_MotorReadEncoder(void)
{
    return (int32_t)__HAL_TIM_GET_COUNTER(&htim3);
}

static void App_MotorResetPid(void)
{
    s_motorPid.integral = 0.0f;
    s_motorPid.previousError = 0.0f;
    s_startupBoostRemainingMs = 0U;
}

static void App_MotorResetSpeedEstimator(void)
{
    uint8_t index;

    s_speedAccumCount = 0;
    s_speedSampleCount = 0U;
    s_speedSampleIndex = 0U;
    for (index = 0U; index < APP_MOTOR_SPEED_AVG_SAMPLES; index++)
    {
        s_speedDeltaWindow[index] = 0;
    }
    s_filteredRpm = 0.0f;
}

static void App_MotorResetController(void)
{
    App_MotorResetPid();
    App_MotorResetSpeedEstimator();
    s_lastDuty = 0.0f;
    s_stopNoPulseElapsedMs = 0U;
    s_speedLimitActive = 0U;
}

/*
 * 实际转速保护：超过 1000 RPM 时立即关闭 PWM，转速降到 950 RPM 以下后
 * 才允许恢复控制输出。使用回差可以避免在 1000 RPM 附近反复启停。
 */
static void App_MotorLimitDutyBySpeed(float currentRpm, float *duty)
{
    if (duty == NULL)
    {
        return;
    }

    if (currentRpm >= (float)APP_MOTOR_SPEED_LIMIT_RPM)
    {
        s_speedLimitActive = 1U;
    }
    else if ((s_speedLimitActive != 0U) &&
             (currentRpm <= APP_MOTOR_SPEED_LIMIT_RELEASE_RPM))
    {
        s_speedLimitActive = 0U;
    }

    if ((s_speedLimitActive != 0U) &&
        (*duty > APP_MOTOR_SPEED_LIMIT_DUTY))
    {
        *duty = APP_MOTOR_SPEED_LIMIT_DUTY;
    }
}

static float App_MotorUpdateRpm(int32_t deltaCount)
{
    float sampleMinutes;

    /*
     * 使用 8 点滑动窗口累计编码器增量。窗口长度仍为 400 ms，保留低速
     * 测量分辨率，但每 50 ms 都会得到一个新转速，避免原实现每 400 ms
     * 才更新一次反馈导致 PID 响应迟缓。
     */
    if (s_speedSampleCount < APP_MOTOR_SPEED_AVG_SAMPLES)
    {
        s_speedSampleCount++;
    }
    else
    {
        s_speedAccumCount -= s_speedDeltaWindow[s_speedSampleIndex];
    }

    s_speedDeltaWindow[s_speedSampleIndex] = deltaCount;
    s_speedAccumCount += deltaCount;
    s_speedSampleIndex++;
    if (s_speedSampleIndex >= APP_MOTOR_SPEED_AVG_SAMPLES)
    {
        s_speedSampleIndex = 0U;
    }

    sampleMinutes = ((float)APP_MOTOR_CONTROL_PERIOD_MS *
                     (float)s_speedSampleCount) / 60000.0f;
    s_filteredRpm = ((float)s_speedAccumCount /
                     APP_MOTOR_ENCODER_COUNTS_PER_REV) / sampleMinutes;
    if (s_filteredRpm < 0.0f)
    {
        s_filteredRpm = -s_filteredRpm;
    }

    return s_filteredRpm;
}

static float App_MotorFeedForwardDuty(float targetRpm)
{
    float duty;

    /* 目标为零时必须保持零输出，不能把基础占空比直接送到电机。 */
    if (targetRpm <= 0.0f)
    {
        return 0.0f;
    }

    duty = APP_MOTOR_FF_BASE_DUTY +
           targetRpm * APP_MOTOR_FF_DUTY_PER_RPM;
    if (duty > APP_MOTOR_FF_MAX_DUTY)
    {
        duty = APP_MOTOR_FF_MAX_DUTY;
    }

    return duty;
}

static float App_MotorPidStep(float targetRpm, float currentRpm)
{
    float error;
    float derivative;
    float integralCandidate;
    float rawCorrection;
    float correction;
    float output;

    error = targetRpm - currentRpm;
    integralCandidate = s_motorPid.integral +
                        error * ((float)APP_MOTOR_CONTROL_PERIOD_MS / 1000.0f);
    if (integralCandidate > APP_MOTOR_PID_INTEGRAL_LIMIT)
    {
        integralCandidate = APP_MOTOR_PID_INTEGRAL_LIMIT;
    }
    else if (integralCandidate < -APP_MOTOR_PID_INTEGRAL_LIMIT)
    {
        integralCandidate = -APP_MOTOR_PID_INTEGRAL_LIMIT;
    }

    derivative = (error - s_motorPid.previousError) /
                 ((float)APP_MOTOR_CONTROL_PERIOD_MS / 1000.0f);
    rawCorrection = (APP_MOTOR_PID_KP * error +
                     APP_MOTOR_PID_KI * integralCandidate +
                     APP_MOTOR_PID_KD * derivative) /
                    APP_MOTOR_PID_OUTPUT_SCALE;

    /*
     * 当 PID 已经达到修正上限且误差还在推动积分继续增大时，冻结积分。
     * 这样可避免长时间饱和后产生积分堆积，导致转速回到目标附近仍迟迟降不下来。
     */
    if (((rawCorrection > APP_MOTOR_PID_MAX_CORRECTION) && (error > 0.0f)) ||
        ((rawCorrection < -APP_MOTOR_PID_MAX_CORRECTION) && (error < 0.0f)))
    {
        rawCorrection = (APP_MOTOR_PID_KP * error +
                         APP_MOTOR_PID_KI * s_motorPid.integral +
                         APP_MOTOR_PID_KD * derivative) /
                        APP_MOTOR_PID_OUTPUT_SCALE;
    }
    else
    {
        s_motorPid.integral = integralCandidate;
    }
    correction = rawCorrection;
    /*
     * 前馈负责主要输出，按标定曲线最多提供 90% 占空比；PID 只修正
     * 负载扰动和模型误差，修正量限制为 +/-10%，避免积分或突发误差
     * 把输出直接推到不可控的满量程。
     */
    output = App_MotorFeedForwardDuty(targetRpm);

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
        /* 最终输出闸门同时检查运行状态、目标转速和全部故障位。 */
        if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
            (g_appData.FaultFlags == APP_FAULT_NONE) &&
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
        /* 故障状态立即归零；IDLE 状态允许发布电机自然减速期间的实测转速。 */
        if (g_appData.CurrentStatus != APP_MOTOR_STATUS_FAULT)
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
    TickType_t lastControlTick;
#if (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_VOFA_ENABLE)
    TickType_t lastVofaTick;
#elif (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_DEBUG_ENABLE)
    TickType_t lastDebugTick;
#endif

#if (APP_MOTOR_LOG_ENABLE == 0U)
    /* 关闭日志时仍保留占空比缓存，显式读取以避免 ARMCC 未使用变量告警。 */
    (void)s_lastDuty;
#endif

    (void)argument;
    encoderCount = App_MotorReadEncoder();
    s_lastEncoderCount = encoderCount;
    App_MotorResetController();
    lastControlTick = xTaskGetTickCount();
#if (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_VOFA_ENABLE)
    lastVofaTick = lastControlTick;
#elif (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_DEBUG_ENABLE)
    lastDebugTick = lastControlTick;
#endif

    for (;;)
    {
        /* 固定 50 ms 控制节拍，保证编码器转速换算和 PID 的 dt 与实际周期一致。 */
        vTaskDelayUntil(&lastControlTick,
                        pdMS_TO_TICKS(APP_MOTOR_CONTROL_PERIOD_MS));

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
            /* 即使旧 EEPROM 或通信写入带入更大的值，控制器也只允许 1000 RPM。 */
            if (targetRpm > (float)APP_MOTOR_SPEED_LIMIT_RPM)
            {
                targetRpm = (float)APP_MOTOR_SPEED_LIMIT_RPM;
            }
            else if (targetRpm < (float)APP_MOTOR_SPEED_MIN_RPM)
            {
                /* 控制层最后一道边界保护：任何绕过设置入口的非零低速目标都按 200 RPM 运行。 */
                targetRpm = (float)APP_MOTOR_SPEED_MIN_RPM;
            }
            s_stopNoPulseElapsedMs = 0U;
            /*
             * 电机从静止启动时短暂提供 70% 助推，克服静摩擦；转速达到目标
             * 的 80% 或最长 300 ms 后立即退出助推，再交给前馈 + PID 控制。
             */
            if (s_pwmStarted == 0U)
            {
                s_startupBoostRemainingMs = APP_MOTOR_STARTUP_BOOST_TIME_MS;
            }

            if ((s_startupBoostRemainingMs > 0U) &&
                (currentRpm < (targetRpm * APP_MOTOR_STARTUP_EXIT_RATIO)))
            {
                duty = App_MotorFeedForwardDuty(targetRpm);
                if (duty < APP_MOTOR_STARTUP_BOOST_DUTY)
                {
                    duty = APP_MOTOR_STARTUP_BOOST_DUTY;
                }
                s_motorPid.integral = 0.0f;
                s_motorPid.previousError = targetRpm - currentRpm;

                if (s_startupBoostRemainingMs > APP_MOTOR_CONTROL_PERIOD_MS)
                {
                    s_startupBoostRemainingMs -= APP_MOTOR_CONTROL_PERIOD_MS;
                }
                else
                {
                    s_startupBoostRemainingMs = 0U;
                }
            }
            else
            {
                s_startupBoostRemainingMs = 0U;
                duty = App_MotorPidStep(targetRpm, currentRpm);
            }

            /* 实际转速超过安全阈值时覆盖控制输出，优先保证机械安全。 */
            App_MotorLimitDutyBySpeed(currentRpm, &duty);

            if (App_MotorApplyDutyIfRunning(duty) == 0U)
            {
                App_MotorResetPid();
                App_MotorStop();
            }
        }
        else
        {
            /* 停止时只复位 PID，不能清掉速度估算器，便于观察机械减速过程。 */
            App_MotorResetPid();
            App_MotorStop();
            if (status == APP_MOTOR_STATUS_FAULT)
            {
                /* 故障状态要求速度立即清零，不保留减速显示。 */
                App_MotorResetSpeedEstimator();
                s_stopNoPulseElapsedMs = APP_MOTOR_STOP_SPEED_TIMEOUT_MS;
            }
            else if (targetRpm == 0.0f)
            {
                /* 目标被清零时不再显示旧的转速。 */
                App_MotorResetSpeedEstimator();
                s_stopNoPulseElapsedMs = APP_MOTOR_STOP_SPEED_TIMEOUT_MS;
            }
            else if (deltaCount == 0)
            {
                if (s_stopNoPulseElapsedMs <=
                    (APP_MOTOR_STOP_SPEED_TIMEOUT_MS - APP_MOTOR_CONTROL_PERIOD_MS))
                {
                    s_stopNoPulseElapsedMs += APP_MOTOR_CONTROL_PERIOD_MS;
                }
                else
                {
                    s_stopNoPulseElapsedMs = APP_MOTOR_STOP_SPEED_TIMEOUT_MS;
                }
            }
            else
            {
                /* 停止后仍检测到脉冲，说明转子还在转动，重新开始等待无脉冲超时。 */
                s_stopNoPulseElapsedMs = 0U;
            }
        }

        speedToStore = (int16_t)(currentRpm + 0.5f);
        if ((status == APP_MOTOR_STATUS_FAULT) ||
            (targetRpm <= 0.0f) ||
            (s_stopNoPulseElapsedMs >= APP_MOTOR_STOP_SPEED_TIMEOUT_MS))
        {
            speedToStore = 0;
        }
        App_MotorUpdateCurrentSpeed(speedToStore);

#if (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_VOFA_ENABLE)
        if ((xTaskGetTickCount() - lastVofaTick) >=
            pdMS_TO_TICKS(APP_MOTOR_VOFA_PERIOD_MS))
        {
            lastVofaTick = xTaskGetTickCount();

            /*
             * FireWater 四通道顺序：目标转速、当前转速、PWM 占空比、编码器增量。
             * 空闲状态也持续发送零输出，便于在 VOFA 中观察完整的启动和停止过程。
             */
            Com_VofaSendMotorFrame((int16_t)(targetRpm + 0.5f),
                                   speedToStore,
                                   (uint16_t)(s_lastDuty * 100.0f),
                                   deltaCount);
        }
#elif (APP_MOTOR_LOG_ENABLE != 0U) && defined(COM_DEBUG_ENABLE)
        /* 恢复普通调试模式时，保留原有的人类可读电机日志。 */
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
#endif

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

void App_MotorStop(void)
{
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3);
    HAL_GPIO_WritePin(MOTOR2_GPIO_Port, MOTOR2_Pin, MOTOR2_FIXED_STATE);
    s_pwmStarted = 0U;
    s_lastDuty = 0.0f;
}

uint8_t App_MotorCreateTask(void)
{
    return (xTaskCreate(App_MotorTask, "Motor", 320U, NULL,
                        tskIDLE_PRIORITY + 2U, NULL) == pdPASS) ? 1U : 0U;
}

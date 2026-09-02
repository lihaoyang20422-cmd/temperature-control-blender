#include "App_system.h"
#include "semphr.h"
#include "stm32f1xx_hal.h"
#include "tim.h"
#include "bsp_pins.h"
#include "App_buzzer.h"

/* 应用层共享状态；调度器启动后，所有读写操作都必须持有互斥锁。 */
AppMotorStatus_t g_motorStatus = { APP_MOTOR_STATUS_IDLE };
AppFocusState_t g_focusState = { APP_FOCUS_TEMPERATURE, APP_FOCUS_TEMPERATURE };
AppData_t g_appData = { 0 };

/* 互斥锁保持为本文件私有，外部只能通过加锁和解锁接口访问。 */
static SemaphoreHandle_t s_appStateMutex;
/* 防止同一次持续故障重复执行安全关闭动作。 */
static uint8_t s_motorFaultHandled;

static void App_SystemForceOutputsOff(void)
{
    /* 统一安全停机：IDLE、完成和 FAULT 下所有危险输出均必须关闭。 */
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);
    (void)HAL_TIM_PWM_Stop(&htim2, TIM_CHANNEL_1);
    __HAL_TIM_SET_COMPARE(&htim4, TIM_CHANNEL_3, 0U);
    (void)HAL_TIM_PWM_Stop(&htim4, TIM_CHANNEL_3);
    HAL_GPIO_WritePin(MOTOR_ON_GPIO_Port, MOTOR_ON_Pin, MOTOR_ON_INACTIVE_STATE);
    HAL_GPIO_WritePin(MOTOR2_GPIO_Port, MOTOR2_Pin, MOTOR2_FIXED_STATE);
}

uint8_t App_SystemInit(void)
{
    if (s_appStateMutex != NULL)
    {
        return 1U;
    }

    s_appStateMutex = xSemaphoreCreateMutex();
    if (s_appStateMutex == NULL)
    {
        return 0U;
    }

    /* 在任何应用任务启动前设置安全默认值。 */
    g_motorStatus.Current = APP_MOTOR_STATUS_IDLE;
    g_focusState.Current = APP_FOCUS_TEMPERATURE;
    g_focusState.Previous = APP_FOCUS_TEMPERATURE;

    g_appData.CurrentTemperature = 0;
    g_appData.CurrentBoardTemperature = 0;
    g_appData.TargetTemperature = 0;
    g_appData.CurrentSpeed = 0;
    g_appData.TargetSpeed = 0;
    g_appData.RemainingTime = 0U;
    g_appData.TargetTime = 0U;
    g_appData.CurrentStatus = APP_MOTOR_STATUS_IDLE;
    g_appData.CurrentTime = 0U;
    g_appData.FaultFlags = APP_FAULT_NONE;

    /* 保存完整的 96 位 UID，避免压缩为 32 位后降低唯一性。 */
    g_appData.Uid[0] = HAL_GetUIDw0();
    g_appData.Uid[1] = HAL_GetUIDw1();
    g_appData.Uid[2] = HAL_GetUIDw2();
    s_motorFaultHandled = 0U;

    return 1U;
}

uint8_t App_SystemLock(TickType_t waitTicks)
{
    if (s_appStateMutex == NULL)
    {
        return 0U;
    }

    return (xSemaphoreTake(s_appStateMutex, waitTicks) == pdTRUE) ? 1U : 0U;
}

void App_SystemUnlock(void)
{
    if (s_appStateMutex != NULL)
    {
        (void)xSemaphoreGive(s_appStateMutex);
    }
}

uint8_t App_SystemSetMotorStatus(AppMotorStatusValue_t status)
{
    uint8_t callFaultHook = 0U;
    uint8_t stopOutputs = 0U;

    if ((status != APP_MOTOR_STATUS_IDLE) &&
        (status != APP_MOTOR_STATUS_RUNNING) &&
        (status != APP_MOTOR_STATUS_FAULT))
    {
        return 0U;
    }

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return 0U;
    }

    /* 两个状态字段必须在同一个临界区内同步更新。 */
    /* 故障位一旦锁存，普通状态接口不得绕过故障恢复流程离开 FAULT。 */
    if ((status != APP_MOTOR_STATUS_FAULT) &&
        (g_appData.FaultFlags != APP_FAULT_NONE))
    {
        App_SystemUnlock();
        return 0U;
    }

    g_motorStatus.Current = status;
    g_appData.CurrentStatus = status;

    if (status == APP_MOTOR_STATUS_FAULT)
    {
        if (s_motorFaultHandled == 0U)
        {
            s_motorFaultHandled = 1U;
            callFaultHook = 1U;
        }
    }
    else
    {
        /* 离开故障状态后允许下一次新故障再次触发安全钩子。 */
        s_motorFaultHandled = 0U;
    }

    if (status != APP_MOTOR_STATUS_RUNNING)
    {
        stopOutputs = 1U;
    }

    App_SystemUnlock();

    /* 状态提交后立即关闭所有危险输出，避免等待各控制任务的下一个周期。 */
    if (stopOutputs != 0U)
    {
        App_SystemForceOutputsOff();
    }

    /* 安全动作放在锁外执行，避免电机驱动操作长期占用状态互斥锁。 */
    if (callFaultHook != 0U)
    {
        App_SystemMotorFaultHook();
    }

    return 1U;
}

uint8_t App_SystemSetFault(AppFault_t fault)
{
    if (fault == APP_FAULT_NONE)
    {
        return 0U;
    }

    /* 必须先撤销硬件输出，再提交共享状态。 */
    App_SystemForceOutputsOff();
    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        App_SystemForceOutputsOff();
        return 0U;
    }

    g_appData.FaultFlags |= (uint32_t)fault;
    g_appData.CurrentStatus = APP_MOTOR_STATUS_FAULT;
    g_motorStatus.Current = APP_MOTOR_STATUS_FAULT;
    s_motorFaultHandled = 1U;
    App_SystemUnlock();

    /* 覆盖等待互斥锁期间可能发生的并发输出。 */
    App_SystemForceOutputsOff();
    /* 统一故障报警：所有故障进入路径均启动持续蜂鸣，接口内部异步处理。 */
    App_BuzzerSetContinuous(1U);
    return 1U;
}

uint8_t App_SystemEnterFault(AppFault_t fault)
{
    /* 所有模块统一从该入口进入故障，确保危险输出先关闭。 */
    return App_SystemSetFault(fault);
}

uint8_t App_SystemClearFault(AppFault_t fault)
{
    uint8_t stopAlarm = 0U;

    if (fault == APP_FAULT_NONE)
    {
        return 0U;
    }

    /* 清除故障前仍保持所有输出关闭，避免恢复瞬间误启动。 */
    App_SystemForceOutputsOff();
    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return 0U;
    }

    g_appData.FaultFlags &= ~((uint32_t)fault);
    if (g_appData.FaultFlags == APP_FAULT_NONE)
    {
        g_appData.CurrentStatus = APP_MOTOR_STATUS_IDLE;
        g_motorStatus.Current = APP_MOTOR_STATUS_IDLE;
        s_motorFaultHandled = 0U;
        /* 仅记录报警状态，实际蜂鸣器操作放到释放状态锁之后。 */
        stopAlarm = 1U;
    }
    App_SystemUnlock();

    /* 仅当所有故障均已清除时停止持续报警，避免覆盖其他模块的故障。 */
    if (stopAlarm != 0U)
    {
        App_BuzzerSetContinuous(0U);
    }
    return 1U;
}

/* 已按产品方案移除未接入的系统安全监控函数，保留以下旧实现以便历史追溯。 */
#if 0
void App_SystemSafetyCheck(void)
{
    uint8_t callFaultHook = 0U;

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }

    /* 任意一个共享状态字段为故障，都按整机电机故障处理。 */
    if ((g_motorStatus.Current == APP_MOTOR_STATUS_FAULT) ||
        (g_appData.CurrentStatus == APP_MOTOR_STATUS_FAULT))
    {
        g_motorStatus.Current = APP_MOTOR_STATUS_FAULT;
        g_appData.CurrentStatus = APP_MOTOR_STATUS_FAULT;

        if (s_motorFaultHandled == 0U)
        {
            s_motorFaultHandled = 1U;
            callFaultHook = 1U;
        }
    }
    else
    {
        /* 兼容外部代码直接恢复状态，允许下一次故障重新触发安全钩子。 */
        s_motorFaultHandled = 0U;
    }

    App_SystemUnlock();

    if (callFaultHook != 0U)
    {
        App_SystemMotorFaultHook();
    }
}

#endif

__weak void App_SystemMotorFaultHook(void)
{
    /*
     * 预留安全处理接口，后续在电机模块中提供同名强定义即可覆盖此函数。
     * 示例安全动作：停止 TIM4 PWM、清零占空比并关闭电机驱动使能。
     */
}


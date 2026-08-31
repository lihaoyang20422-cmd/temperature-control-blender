#include "App_main.h"
#include "Com_debug.h"
#include "App_buzzer.h"
#include "App_system.h"
#include "bsp_pins.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_SYSTEM_TEST_TASK_STACK_SIZE    256U
#define APP_BUZZER_TEST_TASK_STACK_SIZE    128U
#define APP_SYSTEM_TEST_PERIOD_MS          1000U
#define APP_SYSTEM_TEST_TARGET_TEMP        60
#define APP_SYSTEM_TEST_TARGET_SPEED       600
#define APP_SYSTEM_TEST_TARGET_TIME        10U
#define APP_BUZZER_TEST_INTERVAL_MS        5000U
#define APP_BUZZER_TEST_LONG_MS            1000U
/* 置为 1 可启用蜂鸣器测试任务，默认关闭以避免影响系统测试。 */
#define APP_ENABLE_BUZZER_TEST             0U

typedef enum
{
    APP_TASK_PRIORITY_SYSTEM_TEST = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_BUZZER_TEST = tskIDLE_PRIORITY + 1U
} AppTaskPriority_t;

static void App_SystemTestTask(void *argument);
#if (APP_ENABLE_BUZZER_TEST != 0U)
static void App_BuzzerTestTask(void *argument);
#endif
static const char *App_SystemFocusLine(AppFocusItem_t focus);
static const char *App_SystemStatusName(AppMotorStatusValue_t status);

void App_main(void)
{
    if (App_SystemInit() == 0U)
    {
        debug_printfln("System state mutex create failed");
        for (;;)
        {
        }
    }

    if (App_BuzzerInit() == 0U)
    {
        debug_printfln("Buzzer init failed");
        for (;;)
        {
        }
    }

    if (xTaskCreate(App_SystemTestTask, "SystemTest", APP_SYSTEM_TEST_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_SYSTEM_TEST, NULL) != pdPASS)
    {
        debug_printfln("FreeRTOS system test task create failed");
        for (;;)
        {
        }
    }

#if (APP_ENABLE_BUZZER_TEST != 0U)
    if (xTaskCreate(App_BuzzerTestTask, "BuzzerTest", APP_BUZZER_TEST_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_BUZZER_TEST, NULL) != pdPASS)
    {
        debug_printfln("FreeRTOS buzzer test task create failed");
        for (;;)
        {
        }
    }
#endif

    vTaskStartScheduler();

    for (;;)
    {
    }
}

static void App_SystemTestTask(void *argument)
{
    AppData_t dataSnapshot = { 0 };
    AppFocusState_t focusSnapshot = { APP_FOCUS_TEMPERATURE, APP_FOCUS_TEMPERATURE };
    uint8_t changeToIdle;

    (void)argument;

    /* 测试开始时进入运行状态，并设置目标温度、转速和执行时间。 */
    if (App_SystemSetMotorStatus(APP_MOTOR_STATUS_RUNNING) != 0U)
    {
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            if (g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING)
            {
                /* 当前温度和当前转速暂不测试，保持为 0。 */
                g_appData.CurrentTemperature = 0;
                g_appData.TargetTemperature = APP_SYSTEM_TEST_TARGET_TEMP;
                g_appData.CurrentSpeed = 0;
                g_appData.TargetSpeed = APP_SYSTEM_TEST_TARGET_SPEED;
                g_appData.TargetTime = APP_SYSTEM_TEST_TARGET_TIME;
                g_appData.RemainingTime = g_appData.TargetTime;
            }

            App_SystemUnlock();
        }
    }

    for (;;)
    {
        /* 锁内只复制共享数据，串口打印放在锁外执行，避免长时间占用互斥锁。 */
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            dataSnapshot = g_appData;
            focusSnapshot = g_focusState;
            App_SystemUnlock();
        }

        Com_DebugPrintf("\r\n"
                        "UID   : %08lX%08lX%08lX\r\n"
                        "%s\r\n"
                        "TEMP  : %d / %d C\r\n"
                        "SPEED : %d / %d RPM\r\n"
                        "TIME  : %lu / %lu s\r\n"
                        "STATUS: %s\r\n",
                        (unsigned long)dataSnapshot.Uid[0],
                        (unsigned long)dataSnapshot.Uid[1],
                        (unsigned long)dataSnapshot.Uid[2],
                        App_SystemFocusLine(focusSnapshot.Current),
                        (int)dataSnapshot.CurrentTemperature,
                        (int)dataSnapshot.TargetTemperature,
                        (int)dataSnapshot.CurrentSpeed,
                        (int)dataSnapshot.TargetSpeed,
                        (unsigned long)dataSnapshot.RemainingTime,
                        (unsigned long)dataSnapshot.TargetTime,
                        App_SystemStatusName(dataSnapshot.CurrentStatus));

        vTaskDelay(pdMS_TO_TICKS(APP_SYSTEM_TEST_PERIOD_MS));

        /* 周期执行安全检查，后续可将该调用移入独立的系统监控任务。 */
        App_SystemSafetyCheck();

        /* 只有运行状态才允许更新倒计时和运行状态。 */
        changeToIdle = 0U;
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            if (g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING)
            {
                if (g_appData.RemainingTime > 0U)
                {
                    g_appData.RemainingTime--;
                }

                if (g_appData.RemainingTime == 0U)
                {
                    changeToIdle = 1U;
                }
            }
            App_SystemUnlock();
        }

        if (changeToIdle != 0U)
        {
            (void)App_SystemSetMotorStatus(APP_MOTOR_STATUS_IDLE);
        }
    }
}

static const char *App_SystemFocusLine(AppFocusItem_t focus)
{
    switch (focus)
    {
        case APP_FOCUS_TEMPERATURE:
            return "FOCUS : [TEMP] SPEED TIME";

        case APP_FOCUS_SPEED:
            return "FOCUS : TEMP [SPEED] TIME";

        case APP_FOCUS_TIME:
            return "FOCUS : TEMP SPEED [TIME]";

        default:
            return "FOCUS : TEMP SPEED TIME";
    }
}

static const char *App_SystemStatusName(AppMotorStatusValue_t status)
{
    switch (status)
    {
        case APP_MOTOR_STATUS_IDLE:
            return "IDLE";

        case APP_MOTOR_STATUS_RUNNING:
            return "RUNNING";

        case APP_MOTOR_STATUS_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}

#if (APP_ENABLE_BUZZER_TEST != 0U)
static void App_BuzzerTestTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_INTERVAL_MS));
        debug_printfln("Buzzer short beep");
        App_BuzzerBeepShort();

        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_INTERVAL_MS));
        debug_printfln("Buzzer long beep start");
        App_BuzzerSetContinuous(1U);
        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_LONG_MS));
        App_BuzzerSetContinuous(0U);
        debug_printfln("Buzzer long beep stop");
    }
}
#endif

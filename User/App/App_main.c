#include "App_main.h"
#include "Com_debug.h"
#include "App_buzzer.h"
#include "App_system.h"
#include "App_m24c02.h"
#include "App_storage.h"
#include "App_oled.h"
#include "App_key.h"
#include "App_ui.h"
#include "Dri_key.h"
#include "bsp_pins.h"
#include "Int_I2C1.h"
#include "Int_I2C2.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_SYSTEM_TEST_TASK_STACK_SIZE    256U
#define APP_BUZZER_TEST_TASK_STACK_SIZE    128U
#define APP_SYSTEM_TEST_PERIOD_MS          1000U
#define APP_RUN_CONTROL_TASK_STACK_SIZE    192U
#define APP_RUN_CONTROL_PERIOD_MS          1000U
#define APP_SYSTEM_TEST_TARGET_TEMP        60
#define APP_SYSTEM_TEST_TARGET_SPEED       600
#define APP_SYSTEM_TEST_TARGET_TIME        10U
#define APP_BUZZER_TEST_INTERVAL_MS        5000U
#define APP_BUZZER_TEST_LONG_MS            1000U
/* 开发阶段置为 1，验证完成后置为 0 即可关闭 EEPROM 上电自检。 */
#define APP_ENABLE_M24C02_TEST             0U
#define APP_ENABLE_SYSTEM_TEST             0U
#define APP_M24C02_TEST_TASK_STACK_SIZE    160U
#define APP_M24C02_TEST_START_DELAY_MS     100U
#define APP_STORAGE_TASK_STACK_SIZE         192U
#define APP_STORAGE_DEMO_TASK_STACK_SIZE    192U
#define APP_STORAGE_DEMO_DELAY_MS           1000U
#define APP_STORAGE_DEMO_SAVE_WAIT_MS       6000U
#define APP_KEY_TASK_STACK_SIZE             256U
#define APP_KEY_SCAN_PERIOD_MS              10U
/* 开发验证置为 1；测试完成后置为 0，避免启动时修改用户设置。 */
#define APP_ENABLE_STORAGE_DEMO             0U
#define APP_STORAGE_DEMO_TEMP              50
#define APP_STORAGE_DEMO_SPEED             500
#define APP_STORAGE_DEMO_TIME              60U

typedef enum
{
    APP_TASK_PRIORITY_SYSTEM_TEST = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_BUZZER_TEST = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_M24C02_TEST = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_STORAGE = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_STORAGE_DEMO = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_UI = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_KEY = tskIDLE_PRIORITY + 1U
} AppTaskPriority_t;

static void App_KeyTask(void *argument);
static void App_RunControlTask(void *argument);
#if (APP_ENABLE_SYSTEM_TEST != 0U)
static void App_SystemTestTask(void *argument);
#endif
#if (APP_ENABLE_STORAGE_DEMO != 0U)
static void App_StorageDemoTask(void *argument);
#endif
#if (APP_ENABLE_M24C02_TEST != 0U)
static void App_M24C02TestTask(void *argument);
static const char *App_M24C02TestResultName(AppM24C02TestResult_t result);
#endif
#if (APP_ENABLE_SYSTEM_TEST != 0U)
static const char *App_SystemFocusLine(AppFocusItem_t focus);
static const char *App_SystemStatusName(AppMotorStatusValue_t status);
#endif

void App_main(void)
{
    uint8_t oledReady = 0U;
    uint8_t keyReady = 0U;

    /* 在任何 EEPROM 或其他 I2C2 任务启动前创建总线互斥锁。 */
    if (Bsp_I2c2Init() == 0U)
    {
        debug_printfln("I2C2 bus mutex create failed");
        for (;;)
        {
        }
    }

    /* OLED 使用 I2C1，先创建总线互斥锁，再执行 OLED 初始化。 */
    if (Bsp_I2c1Init() == 0U)
    {
        debug_printfln("I2C1 bus mutex create failed");
    }
    else if (App_OledInit() == 0U)
    {
        debug_printfln("OLED init failed");
    }
    else
    {
        oledReady = 1U;
    }

    if (Dri_KeyInit() == 0U)
    {
        debug_printfln("Key init failed");
    }
    else
    {
        keyReady = 1U;
    }

    if (App_SystemInit() == 0U)
    {
        debug_printfln("System state mutex create failed");
        for (;;)
        {
        }
    }

    switch (App_StorageInit())
    {
        case APP_STORAGE_INIT_LOADED:
            debug_printfln("User settings loaded");
            break;
        case APP_STORAGE_INIT_DEFAULT:
            debug_printfln("No valid user settings, use defaults");
            break;
        case APP_STORAGE_INIT_ERROR:
            debug_printfln("User settings EEPROM unavailable");
            break;
        default:
            debug_printfln("User settings init failed");
            break;
    }

    if (App_BuzzerInit() == 0U)
    {
        debug_printfln("Buzzer init failed");
        for (;;)
        {
        }
    }

#if (APP_ENABLE_SYSTEM_TEST != 0U)
    if (xTaskCreate(App_SystemTestTask, "SystemTest", APP_SYSTEM_TEST_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_SYSTEM_TEST, NULL) != pdPASS)
    {
        debug_printfln("FreeRTOS system test task create failed");
        for (;;)
        {
        }
    }
#endif

#if (APP_ENABLE_M24C02_TEST != 0U)
    if (xTaskCreate(App_M24C02TestTask, "M24C02Test", APP_M24C02_TEST_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_M24C02_TEST, NULL) != pdPASS)
    {
        debug_printfln("M24C02 test task create failed");
        for (;;)
        {
        }
    }
#endif

    if (xTaskCreate(App_StorageTask, "Storage", APP_STORAGE_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_STORAGE, NULL) != pdPASS)
    {
        debug_printfln("Storage task create failed");
        for (;;)
        {
        }
    }

    /* 运行控制任务每秒更新一次已运行/剩余时间，并在到点时自动回到 IDLE。 */
    if (xTaskCreate(App_RunControlTask, "RunControl", APP_RUN_CONTROL_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_SYSTEM_TEST, NULL) != pdPASS)
    {
        debug_printfln("Run control task create failed");
        for (;;)
        {
        }
    }

#if (APP_ENABLE_STORAGE_DEMO != 0U)
    if (xTaskCreate(App_StorageDemoTask, "StorageDemo", APP_STORAGE_DEMO_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_STORAGE_DEMO, NULL) != pdPASS)
    {
        debug_printfln("Storage demo task create failed");
        for (;;)
        {
        }
    }
#endif

    if (oledReady != 0U)
    {
        if (xTaskCreate(App_UiTask, "UI", 256U, NULL, APP_TASK_PRIORITY_UI, NULL) != pdPASS)
        {
            debug_printfln("OLED task create failed");
        }
    }

    if (keyReady != 0U)
    {
        if (xTaskCreate(App_KeyTask, "Key", APP_KEY_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_KEY, NULL) != pdPASS)
        {
            debug_printfln("Key task create failed");
        }
    }

    vTaskStartScheduler();

    for (;;)
    {
    }
}

static void App_KeyTask(void *argument)
{
    DriKeyEvent_t event;

    (void)argument;

    for (;;)
    {
        if (Dri_KeyScan(&event) != 0U)
        {
            App_KeyHandleEvent(&event);
        }

        vTaskDelay(pdMS_TO_TICKS(APP_KEY_SCAN_PERIOD_MS));
    }
}

static void App_RunControlTask(void *argument)
{
    uint8_t finished;

    (void)argument;

    for (;;)
    {
        finished = 0U;

        /* 只在锁内更新共享状态，I/O 和 UI 刷新均在释放锁后进行。 */
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
                (g_appData.RemainingTime > 0U))
            {
                g_appData.RemainingTime--;
                if (g_appData.CurrentTime < g_appData.TargetTime)
                {
                    g_appData.CurrentTime++;
                }

                if (g_appData.RemainingTime == 0U)
                {
                    g_appData.CurrentTime = g_appData.TargetTime;
                    g_appData.CurrentStatus = APP_MOTOR_STATUS_IDLE;
                    g_motorStatus.Current = APP_MOTOR_STATUS_IDLE;
                    finished = 1U;
                }
            }
            App_SystemUnlock();
        }

        if (finished != 0U)
        {
            debug_printfln("Run complete, return to IDLE");
        }
        App_UiNotify();
        vTaskDelay(pdMS_TO_TICKS(APP_RUN_CONTROL_PERIOD_MS));
    }
}

#if (APP_ENABLE_SYSTEM_TEST != 0U)
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
#endif

#if (APP_ENABLE_SYSTEM_TEST != 0U)
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
#endif

#if (APP_ENABLE_SYSTEM_TEST != 0U)
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
#endif

#if (APP_ENABLE_M24C02_TEST != 0U)
static void App_M24C02TestTask(void *argument)
{
    AppM24C02TestResult_t result;

    (void)argument;

    /* 错开系统任务首次打印，避免两段串口输出相互穿插。 */
    vTaskDelay(pdMS_TO_TICKS(APP_M24C02_TEST_START_DELAY_MS));
    result = App_M24C02SelfTest();

    if (result == APP_M24C02_TEST_OK)
    {
        debug_printfln("M24C02 self-test PASS, address=0xF0, data=11 22 33 44 55");
    }
    else
    {
        Com_DebugPrintf("M24C02 self-test FAIL: %s (%d)\r\n",
                        App_M24C02TestResultName(result),
                        (int)result);
    }

    /* 自检只执行一次，结束后删除自身并释放任务栈。 */
    vTaskDelete(NULL);
}

static const char *App_M24C02TestResultName(AppM24C02TestResult_t result)
{
    switch (result)
    {
        case APP_M24C02_TEST_OK:
            return "OK";
        case APP_M24C02_TEST_DEVICE_NOT_READY:
            return "DEVICE_NOT_READY";
        case APP_M24C02_TEST_BACKUP_READ_FAILED:
            return "BACKUP_READ_FAILED";
        case APP_M24C02_TEST_WRITE_FAILED:
            return "WRITE_FAILED";
        case APP_M24C02_TEST_READ_FAILED:
            return "READ_FAILED";
        case APP_M24C02_TEST_DATA_MISMATCH:
            return "DATA_MISMATCH";
        case APP_M24C02_TEST_RESTORE_WRITE_FAILED:
            return "RESTORE_WRITE_FAILED";
        case APP_M24C02_TEST_RESTORE_READ_FAILED:
            return "RESTORE_READ_FAILED";
        case APP_M24C02_TEST_RESTORE_MISMATCH:
            return "RESTORE_MISMATCH";
        default:
            return "UNKNOWN";
    }
}
#endif

#if (APP_ENABLE_STORAGE_DEMO != 0U)
static void App_StorageDemoTask(void *argument)
{
    AppData_t dataSnapshot;

    (void)argument;

    /* 等待存储初始化任务完成首次调度，再打印上电恢复的设置。 */
    vTaskDelay(pdMS_TO_TICKS(APP_STORAGE_DEMO_DELAY_MS));
    if (App_SystemLock(portMAX_DELAY) != 0U)
    {
        dataSnapshot = g_appData;
        App_SystemUnlock();

        Com_DebugPrintf("Storage demo restored: temp=%d, speed=%d, time=%lu s\r\n",
                        (int)dataSnapshot.TargetTemperature,
                        (int)dataSnapshot.TargetSpeed,
                        (unsigned long)dataSnapshot.TargetTime);
    }

    /* 写入一组固定值，验证设置接口和延迟防抖保存功能。 */
    if (App_StorageSetSettings(APP_STORAGE_DEMO_TEMP,
                               APP_STORAGE_DEMO_SPEED,
                               APP_STORAGE_DEMO_TIME) != 0U)
    {
        debug_printfln("Storage demo set: temp=50, speed=500, time=60 s");
    }

    /* 等待超过防抖时间，确保存储任务已经完成 EEPROM 写入。 */
    vTaskDelay(pdMS_TO_TICKS(APP_STORAGE_DEMO_SAVE_WAIT_MS));
    if (App_SystemLock(portMAX_DELAY) != 0U)
    {
        dataSnapshot = g_appData;
        App_SystemUnlock();

        Com_DebugPrintf("Storage demo current: temp=%d, speed=%d, time=%lu s\r\n",
                        (int)dataSnapshot.TargetTemperature,
                        (int)dataSnapshot.TargetSpeed,
                        (unsigned long)dataSnapshot.TargetTime);
    }

    /* Demo 只执行一次，结束后删除自身。 */
    vTaskDelete(NULL);
}
#endif

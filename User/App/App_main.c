#include "App_main.h"
#include "Com_debug.h"
#include "App_buzzer.h"
#include "App_system.h"
#include "App_storage.h"
#include "App_oled.h"
#include "App_motor.h"
#include "App_heater.h"
#include "App_key.h"
#include "App_ui.h"
#include "App_rtc.h"
#include "App_bluetooth.h"
#include "App_imu.h"
#include "App_can.h"
#include "App_modbus.h"
#include "Dri_key.h"
#include "Int_I2C1.h"
#include "Int_I2C2.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_RUN_CONTROL_TASK_STACK_SIZE 192U
#define APP_RUN_CONTROL_PERIOD_MS       1000U
#define APP_STORAGE_TASK_STACK_SIZE     192U
#define APP_KEY_TASK_STACK_SIZE         256U
#define APP_KEY_SCAN_PERIOD_MS          10U
#define APP_MOTOR_ENCODER_TEST_ENABLE   0U

typedef enum
{
    APP_TASK_PRIORITY_STORAGE = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_RUN_CONTROL = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_UI = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_KEY = tskIDLE_PRIORITY + 1U
} AppTaskPriority_t;

static void App_KeyTask(void *argument);
static void App_RunControlTask(void *argument);

void App_main(void)
{
    uint8_t oledReady = 0U;
    uint8_t keyReady = 0U;

    /* 在 EEPROM、RTC 等 I2C2 设备任务启动前创建总线互斥锁。 */
    if (Bsp_I2c2Init() == 0U)
    {
        debug_printfln("I2C2 bus mutex create failed");
        for (;;)
        {
        }
    }
    /* OLED 使用 I2C1，初始化总线互斥锁后再访问显示器。 */
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
        /* 开机画面保持 2 秒，再进入正式 UI。 */
        /* 调度器启动后由 UI 任务持续显示首页，直到用户短按 KEY4。 */
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

    /* 系统互斥锁创建后再读取 RTC，确保时间可以安全发布到全局状态。 */
    if (App_RtcInit() == 0U)
    {
        debug_printfln("RTC init failed");
    }
    else if (App_RtcCreateTask() == 0U)
    {
        debug_printfln("RTC task create failed");
    }

    /* ECB01C 通过 USART2 透传蓝牙命令，蓝牙任务只负责解析并调用应用层接口。 */
    if (App_BluetoothInit() == 0U)
    {
        debug_printfln("Bluetooth init failed");
    }
    else if (App_BluetoothCreateTask() == 0U)
    {
        debug_printfln("Bluetooth task create failed");
    }

    /* CAN1和UART4 Modbus均在调度器启动前完成协议栈初始化，再创建各自任务。 */
    if (App_CanInit() == 0U)
    {
        debug_printfln("CAN init failed");
    }
    else if (App_CanCreateTask() == 0U)
    {
        debug_printfln("CAN task create failed");
    }

    if (App_ModbusInit() == 0U)
    {
        debug_printfln("Modbus init failed");
    }
    else if (App_ModbusCreateTask() == 0U)
    {
        debug_printfln("Modbus task create failed");
    }

    /* 验证 LSM6DSM 通信并创建倾倒检测任务。 */
    if (App_ImuInit() == 0U)
    {
        debug_printfln("IMU bring-up failed");
    }
    else if (App_ImuCreateTask() == 0U)
    {
        debug_printfln("IMU task create failed");
    }

    /* 初始化编码器和闭环 PWM，正式电机任务稍后无条件创建。 */
    if (App_MotorInit() == 0U)
    {
        debug_printfln("Motor init failed");
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
    else
    {
        /* 调度器启动前播放一次开机提示音。 */
        App_BuzzerStartupSound();
    }

    if (App_HeaterInit() == 0U)
    {
        debug_printfln("Heater init failed");
        for (;;)
        {
        }
    }
    if (App_HeaterCreateTask() == 0U)
    {
        debug_printfln("Heater task create failed");
        for (;;)
        {
        }
    }

    /* 根据条件编译选择临时编码器验证任务或正式电机闭环任务。 */
#ifdef APP_MOTOR_ENCODER_TEST_ENABLE
    /* 编码器验证期间只创建累计计数任务，避免与PID控制任务同时读取编码器。 */
#if (APP_MOTOR_ENCODER_TEST_ENABLE != 0U)
    if (App_MotorCreateEncoderTestTask() == 0U)
    {
        debug_printfln("Encoder test task create failed");
        for (;;)
        {
        }
    }
#endif
#else
    if (App_MotorCreateTask() == 0U)
    {
        debug_printfln("Motor task create failed");
        for (;;)
        {
        }
    }
#endif

    if (xTaskCreate(App_StorageTask, "Storage", APP_STORAGE_TASK_STACK_SIZE, NULL,
                    APP_TASK_PRIORITY_STORAGE, NULL) != pdPASS)
    {
        debug_printfln("Storage task create failed");
        for (;;)
        {
        }
    }
    if (xTaskCreate(App_RunControlTask, "RunControl", APP_RUN_CONTROL_TASK_STACK_SIZE, NULL,
                    APP_TASK_PRIORITY_RUN_CONTROL, NULL) != pdPASS)
    {
        debug_printfln("Run control task create failed");
        for (;;)
        {
        }
    }
    if (oledReady != 0U)
    {
        if (xTaskCreate(App_UiTask, "UI", 256U, NULL, APP_TASK_PRIORITY_UI, NULL) != pdPASS)
        {
            debug_printfln("OLED task create failed");
        }
    }
    if (keyReady != 0U)
    {
        if (xTaskCreate(App_KeyTask, "Key", APP_KEY_TASK_STACK_SIZE, NULL,
                        APP_TASK_PRIORITY_KEY, NULL) != pdPASS)
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
        /* 锁内只更新共享计时数据，所有硬件操作放在释放锁之后。 */
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
                    finished = 1U;
                }
            }
            App_SystemUnlock();
        }

        if (finished != 0U)
        {
            /* 完成后统一关闭加热、电机、风扇和方向控制输出。 */
            (void)App_SystemSetMotorStatus(APP_MOTOR_STATUS_IDLE);
            debug_printfln("Run complete, return to IDLE");
        }
        App_UiNotify();
        vTaskDelay(pdMS_TO_TICKS(APP_RUN_CONTROL_PERIOD_MS));
    }
}

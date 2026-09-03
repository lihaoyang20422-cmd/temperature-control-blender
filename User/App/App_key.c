#include <stdint.h>
#include "App_key.h"
#include "App_buzzer.h"
#include "App_system.h"
#include "App_storage.h"
#include "App_ui.h"
#include "App_imu.h"
#include "App_heater.h"
#include "Com_debug.h"

#define APP_KEY_TARGET_TEMP_MAX       150
#define APP_KEY_TARGET_SPEED_MAX      3000
#define APP_KEY_TARGET_TIME_MAX       7200U

static void App_KeyChangeFocus(void);
static void App_KeyAdjustTarget(int16_t delta);
static void App_KeyToggleRunIdle(void);
static void App_KeyClearTargets(void);
static void App_KeyClearFault(void);
static void App_KeyLogEvent(const DriKeyEvent_t *event);

static void App_KeyLogEvent(const DriKeyEvent_t *event)
{
    if ((event == NULL) || (event->type == DRI_KEY_EVENT_REPEAT))
    {
        return;
    }

    if (event->type == DRI_KEY_EVENT_SHORT)
    {
        debug_printfln("KEY%u short", (unsigned int)event->key + 1U);
    }
    else if (event->type == DRI_KEY_EVENT_LONG)
    {
        debug_printfln("KEY%u long", (unsigned int)event->key + 1U);
    }
}

void App_KeyHandleEvent(const DriKeyEvent_t *event)
{
    if ((event == NULL) || (event->key >= DRI_KEY_COUNT))
    {
        return;
    }

    App_KeyLogEvent(event);

    /* 开机首页只接受 KEY4 短按进入设置页，其他按键不会修改参数或启动设备。 */
    if (App_UiGetPage() == APP_UI_PAGE_HOME)
    {
        if ((event->key == DRI_KEY_4) &&
            (event->type == DRI_KEY_EVENT_SHORT))
        {
            (void)App_UiEnterSettingsPage();
        }
        return;
    }

    /* 短按或长按确认时播放提示音，重复调节事件不重复鸣叫。故障连续
       报警期间由蜂鸣器模块自动屏蔽该短鸣。 */
    if ((event->type == DRI_KEY_EVENT_SHORT) ||
        (event->type == DRI_KEY_EVENT_LONG))
    {
        /* 暂时关闭按键确认短鸣，保留蜂鸣器开机音效和持续报警接口。 */
        /* App_BuzzerBeepShort(); */
    }

    switch (event->key)
    {
        case DRI_KEY_1:
            /* KEY1 只响应短按，长按不改变焦点。 */
            if (event->type == DRI_KEY_EVENT_SHORT)
            {
                App_KeyChangeFocus();
            }
            else if (event->type == DRI_KEY_EVENT_LONG)
            {
                App_KeyClearFault();
            }
            break;

        case DRI_KEY_2:
            if (event->type == DRI_KEY_EVENT_SHORT)
            {
                App_KeyAdjustTarget(1);
            }
            else if ((event->type == DRI_KEY_EVENT_LONG) ||
                     (event->type == DRI_KEY_EVENT_REPEAT))
            {
                App_KeyAdjustTarget(5);
            }
            break;

        case DRI_KEY_3:
            if (event->type == DRI_KEY_EVENT_SHORT)
            {
                App_KeyAdjustTarget(-1);
            }
            else if ((event->type == DRI_KEY_EVENT_LONG) ||
                     (event->type == DRI_KEY_EVENT_REPEAT))
            {
                App_KeyAdjustTarget(-5);
            }
            break;

        case DRI_KEY_4:
            if (event->type == DRI_KEY_EVENT_SHORT)
            {
                App_KeyToggleRunIdle();
            }
            else if (event->type == DRI_KEY_EVENT_LONG)
            {
                App_KeyClearTargets();
            }
            break;

        default:
            break;
    }

    /* 参数或焦点发生处理后通知 UI，避免等待下一次周期刷新。 */
    App_UiNotify();
}

static void App_KeyChangeFocus(void)
{
    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }

    /* FAULT 状态下禁止 KEY1 切换焦点。 */
    if (g_appData.CurrentStatus != APP_MOTOR_STATUS_FAULT)
    {
        g_focusState.Previous = g_focusState.Current;
        g_focusState.Current = (AppFocusItem_t)((g_focusState.Current + 1U) % 3U);
    }

    App_SystemUnlock();
}

static void App_KeyAdjustTarget(int16_t delta)
{
    int32_t value;
    uint32_t step;
    uint8_t requestIdle = 0U;

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }

    /* 温度和转速在故障状态下仍允许修改目标值，但不会改变故障状态。 */
    if (g_focusState.Current == APP_FOCUS_TEMPERATURE)
    {
        value = (int32_t)g_appData.TargetTemperature + (int32_t)delta;
        if (value < 0)
        {
            value = 0;
        }
        else if (value > APP_KEY_TARGET_TEMP_MAX)
        {
            value = APP_KEY_TARGET_TEMP_MAX;
        }
        g_appData.TargetTemperature = (int16_t)value;
    }
    else if (g_focusState.Current == APP_FOCUS_SPEED)
    {
        value = (int32_t)g_appData.TargetSpeed + (int32_t)delta;
        if (value < 0)
        {
            value = 0;
        }
        else if (value > APP_KEY_TARGET_SPEED_MAX)
        {
            value = APP_KEY_TARGET_SPEED_MAX;
        }
        g_appData.TargetSpeed = (int16_t)value;
    }
    else if (g_focusState.Current == APP_FOCUS_TIME)
    {
        if (delta < 0)
        {
            step = (uint32_t)(-(int32_t)delta);
            if (g_appData.TargetTime > step)
            {
                g_appData.TargetTime -= step;
            }
            else
            {
                g_appData.TargetTime = 0U;
            }
        }
        else
        {
            step = (uint32_t)delta;
            if (g_appData.TargetTime <= (APP_KEY_TARGET_TIME_MAX - step))
            {
                g_appData.TargetTime += step;
            }
            else
            {
                g_appData.TargetTime = APP_KEY_TARGET_TIME_MAX;
            }
        }

        /* 修改设定时间后立即重算剩余时间，避免显示值和倒计时状态不一致。 */
        if (g_appData.CurrentTime > g_appData.TargetTime)
        {
            g_appData.CurrentTime = g_appData.TargetTime;
        }
        g_appData.RemainingTime = g_appData.TargetTime - g_appData.CurrentTime;
        if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
            (g_appData.RemainingTime == 0U))
        {
            requestIdle = 1U;
        }
    }

    App_SystemUnlock();

    if (requestIdle != 0U)
    {
        (void)App_SystemSetMotorStatus(APP_MOTOR_STATUS_IDLE);
    }

    /* 参数变化后重启 5 秒防抖计时，实际 EEPROM 写入由存储任务完成。 */
    App_StorageRequestSave();
}

static void App_KeyToggleRunIdle(void)
{
    AppMotorStatusValue_t nextStatus = APP_MOTOR_STATUS_FAULT;

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }

    /* FAULT 状态下 KEY4 被拒绝，不能通过按键解除故障。 */
    if (g_appData.CurrentStatus != APP_MOTOR_STATUS_FAULT)
    {
        if (g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING)
        {
            nextStatus = APP_MOTOR_STATUS_IDLE;
            /* 短按停止只暂停本次运行，保留已运行时间和剩余时间，便于再次继续。 */
        }
        else if (g_appData.TargetTime != 0U)
        {
            nextStatus = APP_MOTOR_STATUS_RUNNING;
            /* 首次启动或上一轮已完成时从 0 开始；暂停恢复时保留原进度。 */
            if ((g_appData.RemainingTime == 0U) ||
                (g_appData.CurrentTime >= g_appData.TargetTime))
            {
                g_appData.CurrentTime = 0U;
                g_appData.RemainingTime = g_appData.TargetTime;
            }
        }
    }

    App_SystemUnlock();

    /* 统一状态入口会复核故障位，并在切换到 IDLE 时立即关闭全部危险输出。 */
    if (nextStatus != APP_MOTOR_STATUS_FAULT)
    {
        (void)App_SystemSetMotorStatus(nextStatus);
    }
}

static void App_KeyClearTargets(void)
{
    uint8_t changeToIdle = 0U;

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }

    /* 长按 KEY4 在非故障状态下清零目标参数并回到 IDLE。 */
    if (g_appData.CurrentStatus != APP_MOTOR_STATUS_FAULT)
    {
        g_appData.TargetTemperature = 0;
        g_appData.TargetSpeed = 0;
        g_appData.TargetTime = 0U;
        g_appData.CurrentTime = 0U;
        g_appData.RemainingTime = 0U;
        changeToIdle = 1U;
    }

    App_SystemUnlock();
    if (changeToIdle != 0U)
    {
        (void)App_SystemSetMotorStatus(APP_MOTOR_STATUS_IDLE);
    }
    /* 长按清零同样需要持久化，避免复位后恢复旧设置。 */
    App_StorageRequestSave();
}

static void App_KeyClearFault(void)
{
    uint32_t faultFlags;
    uint8_t handled = 0U;

    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return;
    }
    if (g_appData.CurrentStatus != APP_MOTOR_STATUS_FAULT)
    {
        App_SystemUnlock();
        return;
    }
    faultFlags = g_appData.FaultFlags;
    App_SystemUnlock();

    /* 各模块只清除自己拥有的故障位，KEY1 仅负责发起用户确认。 */
    if ((faultFlags & APP_FAULT_IMU_TILT) != 0U)
    {
        handled = (App_ImuTryClearFault() != 0U) ? 1U : handled;
    }

    if ((faultFlags & (APP_FAULT_HEATER_SENSOR |
                       APP_FAULT_HEATER_OVERTEMP |
                       APP_FAULT_HEATER_ELECTRICAL |
                       APP_FAULT_ADC_RUNTIME)) != 0U)
    {
        handled = (App_HeaterTryClearFault() != 0U) ? 1U : handled;
    }

    if (handled == 0U)
    {
        debug_printfln("Fault clear rejected: condition not recovered");
    }
    App_UiNotify();
}

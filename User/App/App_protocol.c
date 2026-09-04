#include "App_protocol.h"

#include "App_system.h"
#include "App_storage.h"
#include "App_ui.h"
#include "App_motor.h"

#define APP_PROTOCOL_TEMP_MAX           150U
#define APP_PROTOCOL_SPEED_MAX          APP_MOTOR_SPEED_LIMIT_RPM
#define APP_PROTOCOL_TIME_SEC_MAX       7200U
#define APP_PROTOCOL_TIME_MIN_MAX       120U
#define APP_PROTOCOL_SECONDS_PER_MINUTE 60U

static uint8_t App_ProtocolValueValid(uint16_t reg, uint16_t value)
{
    switch (reg)
    {
        case APP_REG_TARGET_TEMP: return (value <= APP_PROTOCOL_TEMP_MAX) ? 1U : 0U;
        case APP_REG_TARGET_SPEED:
            /* 0 表示停止；通信设置的非零转速必须位于安全范围。 */
            return ((value == 0U) ||
                    ((value >= APP_MOTOR_SPEED_MIN_RPM) &&
                     (value <= APP_PROTOCOL_SPEED_MAX))) ? 1U : 0U;
        case APP_REG_TARGET_TIME_SEC: return (value <= APP_PROTOCOL_TIME_SEC_MAX) ? 1U : 0U;
        case APP_REG_TARGET_TIME_MIN: return (value <= APP_PROTOCOL_TIME_MIN_MAX) ? 1U : 0U;
        case APP_REG_RUN_CONTROL: return (value <= 2U) ? 1U : 0U;
        default: return 0U;
    }
}

AppProtocolStatus_t App_ProtocolValidateWrite(uint16_t reg, uint16_t value)
{
    return (App_ProtocolValueValid(reg, value) != 0U) ? APP_PROTOCOL_OK : APP_PROTOCOL_BAD_VALUE;
}

AppProtocolStatus_t App_ProtocolReadRegister(uint16_t reg, uint16_t *value)
{
    AppData_t snapshot;

    if (value == NULL)
    {
        return APP_PROTOCOL_BAD_VALUE;
    }
    if (App_SystemLock(portMAX_DELAY) == 0U)
    {
        return APP_PROTOCOL_BAD_VALUE;
    }
    snapshot = g_appData;
    App_SystemUnlock();

    switch (reg)
    {
        case APP_REG_TARGET_TEMP: *value = (uint16_t)snapshot.TargetTemperature; break;
        case APP_REG_TARGET_SPEED: *value = (uint16_t)snapshot.TargetSpeed; break;
        case APP_REG_TARGET_TIME_SEC: *value = (uint16_t)snapshot.TargetTime; break;
        case APP_REG_TARGET_TIME_MIN: *value = (uint16_t)((snapshot.TargetTime + 59U) / 60U); break;
        case APP_REG_RUN_CONTROL:
            *value = (snapshot.CurrentStatus == APP_MOTOR_STATUS_RUNNING) ? 1U : 0U; break;
        case APP_REG_SYSTEM_STATUS: *value = (uint16_t)snapshot.CurrentStatus; break;
        case APP_REG_CURRENT_TEMP: *value = (uint16_t)snapshot.CurrentTemperature; break;
        case APP_REG_CURRENT_SPEED: *value = (uint16_t)snapshot.CurrentSpeed; break;
        case APP_REG_REMAINING_TIME: *value = (uint16_t)snapshot.RemainingTime; break;
        case APP_REG_UID0_HIGH: *value = (uint16_t)(snapshot.Uid[0] >> 16); break;
        case APP_REG_UID0_LOW: *value = (uint16_t)snapshot.Uid[0]; break;
        case APP_REG_UID1_HIGH: *value = (uint16_t)(snapshot.Uid[1] >> 16); break;
        case APP_REG_UID1_LOW: *value = (uint16_t)snapshot.Uid[1]; break;
        case APP_REG_UID2_HIGH: *value = (uint16_t)(snapshot.Uid[2] >> 16); break;
        case APP_REG_UID2_LOW: *value = (uint16_t)snapshot.Uid[2]; break;
        case APP_REG_ELAPSED_TIME: *value = (uint16_t)snapshot.CurrentTime; break;
        case APP_REG_CURRENT_BOARD_TEMP: *value = (uint16_t)snapshot.CurrentBoardTemperature; break;
        case APP_REG_FAULT_FLAGS: *value = (uint16_t)snapshot.FaultFlags; break;
        default: return APP_PROTOCOL_BAD_REG;
    }
    return APP_PROTOCOL_OK;
}

/* 调用者在锁内调用，按目标时间变化同步剩余时间和已运行时间。 */
static void App_ProtocolSetTargetTimeLocked(uint32_t targetTime)
{
    uint32_t previousTarget = g_appData.TargetTime;

    g_appData.TargetTime = targetTime;
    if (previousTarget != targetTime)
    {
        if ((g_appData.CurrentStatus != APP_MOTOR_STATUS_RUNNING) &&
            (previousTarget > 0U) && (g_appData.CurrentTime >= previousTarget) &&
            (g_appData.RemainingTime == 0U))
        {
            g_appData.CurrentTime = 0U;
        }
        if (g_appData.CurrentTime > targetTime)
        {
            g_appData.CurrentTime = targetTime;
        }
        g_appData.RemainingTime = targetTime - g_appData.CurrentTime;
        if ((g_appData.CurrentStatus == APP_MOTOR_STATUS_RUNNING) &&
            (g_appData.RemainingTime == 0U))
        {
            g_appData.CurrentStatus = APP_MOTOR_STATUS_IDLE;
        }
    }
}

static AppProtocolStatus_t App_ProtocolWriteCore(uint16_t reg, uint16_t value,
                                                  uint8_t deferred, uint8_t *needsCommit)
{
    uint8_t changed = 0U;
    uint8_t persist = 0U;
    uint8_t statusAction = 0U;
    AppMotorStatusValue_t requestedStatus = APP_MOTOR_STATUS_IDLE;

    if (App_ProtocolValueValid(reg, value) == 0U || App_SystemLock(portMAX_DELAY) == 0U)
    {
        return APP_PROTOCOL_BAD_VALUE;
    }

    switch (reg)
    {
        case APP_REG_TARGET_TEMP:
            g_appData.TargetTemperature = (int16_t)value; changed = 1U; persist = 1U; break;
        case APP_REG_TARGET_SPEED:
            g_appData.TargetSpeed = (int16_t)value; changed = 1U; persist = 1U; break;
        case APP_REG_TARGET_TIME_SEC:
            App_ProtocolSetTargetTimeLocked(value); changed = 1U; persist = 1U; break;
        case APP_REG_TARGET_TIME_MIN:
            App_ProtocolSetTargetTimeLocked((uint32_t)value * APP_PROTOCOL_SECONDS_PER_MINUTE);
            changed = 1U; persist = 1U; break;
        case APP_REG_RUN_CONTROL:
            if (value == 0U)
            {
                if (g_appData.FaultFlags == APP_FAULT_NONE)
                {
                    requestedStatus = APP_MOTOR_STATUS_IDLE; statusAction = 1U; changed = 1U;
                }
            }
            else if (value == 1U)
            {
                if ((g_appData.FaultFlags == APP_FAULT_NONE) && (g_appData.TargetTime > 0U))
                {
                    if ((g_appData.RemainingTime == 0U) ||
                        (g_appData.CurrentTime >= g_appData.TargetTime))
                    {
                        g_appData.CurrentTime = 0U;
                        g_appData.RemainingTime = g_appData.TargetTime;
                    }
                    requestedStatus = APP_MOTOR_STATUS_RUNNING; statusAction = 1U; changed = 1U;
                }
            }
            else
            {
                g_appData.TargetTemperature = 0;
                g_appData.TargetSpeed = 0;
                g_appData.TargetTime = 0U;
                g_appData.CurrentTime = 0U;
                g_appData.RemainingTime = 0U;
                requestedStatus = APP_MOTOR_STATUS_IDLE; statusAction = 1U;
                changed = 1U; persist = 1U;
            }
            break;
        default:
            break;
    }

    App_SystemUnlock();
    if (changed == 0U)
    {
        return APP_PROTOCOL_BAD_VALUE;
    }
    if (statusAction != 0U)
    {
        if (App_SystemSetMotorStatus(requestedStatus) == 0U)
        {
            return APP_PROTOCOL_BAD_VALUE;
        }
    }
    if (persist != 0U)
    {
        if (deferred != 0U)
        {
            if (needsCommit != NULL)
            {
                *needsCommit = 1U;
            }
        }
        else
        {
            /* 只发起保存请求，实际EEPROM写入由存储任务延迟完成。 */
            App_StorageRequestSave();
        }
    }
    App_UiNotify();
    return APP_PROTOCOL_OK;
}

AppProtocolStatus_t App_ProtocolWriteRegister(uint16_t reg, uint16_t value)
{
    return App_ProtocolWriteCore(reg, value, 0U, NULL);
}

AppProtocolStatus_t App_ProtocolWriteRegisterDeferred(uint16_t reg, uint16_t value,
                                                      uint8_t *needsCommit)
{
    return App_ProtocolWriteCore(reg, value, 1U, needsCommit);
}

uint8_t App_ProtocolCommitDeferred(uint8_t needsCommit)
{
    if (needsCommit != 0U)
    {
        App_StorageRequestSave();
    }
    App_UiNotify();
    return 1U;
}

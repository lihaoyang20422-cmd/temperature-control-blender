#include "App_storage.h"
#include "App_system.h"
#include "App_motor.h"
#include "drv_m24c02.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_STORAGE_MAGIC                  0x2004U
#define APP_STORAGE_VERSION                0x0100U
#define APP_STORAGE_SLOT_A_ADDRESS         0x00U
#define APP_STORAGE_SLOT_B_ADDRESS         0x10U
#define APP_STORAGE_RECORD_SIZE            16U
#define APP_STORAGE_DEBOUNCE_MS            5000U
#define APP_STORAGE_TASK_PERIOD_MS         100U

/* 记录布局：Magic、Version、Sequence、温度、转速、时间、CRC16，共 16 字节。 */
#define APP_STORAGE_OFFSET_MAGIC           0U
#define APP_STORAGE_OFFSET_VERSION         2U
#define APP_STORAGE_OFFSET_SEQUENCE        4U
#define APP_STORAGE_OFFSET_TEMPERATURE     6U
#define APP_STORAGE_OFFSET_SPEED           8U
#define APP_STORAGE_OFFSET_TIME            10U
#define APP_STORAGE_OFFSET_CHECKSUM        14U

typedef struct
{
    int16_t targetTemperature;
    int16_t targetSpeed;
    uint32_t targetTime;
} AppStorageSettings_t;

static AppStorageSettings_t s_lastObserved;
static AppStorageSettings_t s_lastSaved;
static uint16_t s_nextSequence;
static uint8_t s_storageInitialized;
static volatile uint8_t s_saveRequested;

static uint16_t App_StorageGetU16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static uint32_t App_StorageGetU32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void App_StoragePutU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
}

static void App_StoragePutU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

/* CRC-16/CCITT-FALSE：多项式 0x1021，初始值 0xFFFF。 */
static uint16_t App_StorageCrc16(const uint8_t *data, uint16_t size)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < size; index++)
    {
        crc ^= (uint16_t)data[index] << 8;
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 0x8000U) != 0U) ?
                  (uint16_t)((crc << 1) ^ 0x1021U) :
                  (uint16_t)(crc << 1);
        }
    }

    return crc;
}

static uint8_t App_StorageSettingsEqual(const AppStorageSettings_t *left,
                                        const AppStorageSettings_t *right)
{
    return ((left->targetTemperature == right->targetTemperature) &&
            (left->targetSpeed == right->targetSpeed) &&
            (left->targetTime == right->targetTime)) ? 1U : 0U;
}

static uint8_t App_StorageRecordValid(const uint8_t *record)
{
    uint16_t storedChecksum;

    if (App_StorageGetU16(&record[APP_STORAGE_OFFSET_MAGIC]) != APP_STORAGE_MAGIC)
    {
        return 0U;
    }

    if (App_StorageGetU16(&record[APP_STORAGE_OFFSET_VERSION]) != APP_STORAGE_VERSION)
    {
        return 0U;
    }

    storedChecksum = App_StorageGetU16(&record[APP_STORAGE_OFFSET_CHECKSUM]);
    return (App_StorageCrc16(record, APP_STORAGE_OFFSET_CHECKSUM) == storedChecksum) ? 1U : 0U;
}

static void App_StorageDecodeSettings(const uint8_t *record,
                                      AppStorageSettings_t *settings)
{
    settings->targetTemperature = (int16_t)App_StorageGetU16(&record[APP_STORAGE_OFFSET_TEMPERATURE]);
    settings->targetSpeed = (int16_t)App_StorageGetU16(&record[APP_STORAGE_OFFSET_SPEED]);
    settings->targetTime = App_StorageGetU32(&record[APP_STORAGE_OFFSET_TIME]);
}

static void App_StorageEncodeRecord(const AppStorageSettings_t *settings,
                                    uint16_t sequence,
                                    uint8_t *record)
{
    uint16_t checksum;

    App_StoragePutU16(&record[APP_STORAGE_OFFSET_MAGIC], APP_STORAGE_MAGIC);
    App_StoragePutU16(&record[APP_STORAGE_OFFSET_VERSION], APP_STORAGE_VERSION);
    App_StoragePutU16(&record[APP_STORAGE_OFFSET_SEQUENCE], sequence);
    App_StoragePutU16(&record[APP_STORAGE_OFFSET_TEMPERATURE], (uint16_t)settings->targetTemperature);
    App_StoragePutU16(&record[APP_STORAGE_OFFSET_SPEED], (uint16_t)settings->targetSpeed);
    App_StoragePutU32(&record[APP_STORAGE_OFFSET_TIME], settings->targetTime);
    checksum = App_StorageCrc16(record, APP_STORAGE_OFFSET_CHECKSUM);
    App_StoragePutU16(&record[APP_STORAGE_OFFSET_CHECKSUM], checksum);
}

/* 序号采用回绕比较，确保 0xFFFF 后仍能判断新旧记录。 */
static uint8_t App_StorageSequenceIsNewer(uint16_t left, uint16_t right)
{
    return ((int16_t)(left - right) > 0) ? 1U : 0U;
}

static void App_StorageApplySettings(const AppStorageSettings_t *settings)
{
    int16_t targetSpeed;

    targetSpeed = settings->targetSpeed;
    if (targetSpeed < 0)
    {
        /* 防止异常 EEPROM 数据被解释为负转速。 */
        targetSpeed = 0;
    }
    else if ((targetSpeed > 0) &&
        (targetSpeed < (int16_t)APP_MOTOR_SPEED_MIN_RPM))
    {
        /* 兼容旧 EEPROM 中低于最低运行转速的非零设置。 */
        targetSpeed = (int16_t)APP_MOTOR_SPEED_MIN_RPM;
    }
    else if (targetSpeed > (int16_t)APP_MOTOR_SPEED_LIMIT_RPM)
    {
        /* 兼容旧 EEPROM 中可能保存的更大转速，加载时统一钳位到安全上限。 */
        targetSpeed = (int16_t)APP_MOTOR_SPEED_LIMIT_RPM;
    }

    /* App_main 在调度器启动前调用，此时没有任务并发，不能阻塞等待互斥锁。 */
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (App_SystemLock(portMAX_DELAY) != 0U)
        {
            g_appData.TargetTemperature = settings->targetTemperature;
            g_appData.TargetSpeed = targetSpeed;
            g_appData.TargetTime = settings->targetTime;
            App_SystemUnlock();
        }
    }
    else
    {
        g_appData.TargetTemperature = settings->targetTemperature;
        g_appData.TargetSpeed = targetSpeed;
        g_appData.TargetTime = settings->targetTime;
    }
}

static uint8_t App_StorageReadBest(AppStorageSettings_t *settings,
                                   uint16_t *sequence)
{
    uint8_t recordA[APP_STORAGE_RECORD_SIZE];
    uint8_t recordB[APP_STORAGE_RECORD_SIZE];
    uint8_t validA;
    uint8_t validB;

    validA = (M24C02_Read(APP_STORAGE_SLOT_A_ADDRESS, recordA, APP_STORAGE_RECORD_SIZE) != 0U) ? 1U : 0U;
    validB = (M24C02_Read(APP_STORAGE_SLOT_B_ADDRESS, recordB, APP_STORAGE_RECORD_SIZE) != 0U) ? 1U : 0U;
    validA = (validA != 0U) ? App_StorageRecordValid(recordA) : 0U;
    validB = (validB != 0U) ? App_StorageRecordValid(recordB) : 0U;

    if ((validA == 0U) && (validB == 0U))
    {
        return 0U;
    }

    if ((validA != 0U) &&
        ((validB == 0U) ||
         (App_StorageSequenceIsNewer(App_StorageGetU16(&recordA[APP_STORAGE_OFFSET_SEQUENCE]),
                                     App_StorageGetU16(&recordB[APP_STORAGE_OFFSET_SEQUENCE])) != 0U)))
    {
        App_StorageDecodeSettings(recordA, settings);
        *sequence = App_StorageGetU16(&recordA[APP_STORAGE_OFFSET_SEQUENCE]);
    }
    else
    {
        App_StorageDecodeSettings(recordB, settings);
        *sequence = App_StorageGetU16(&recordB[APP_STORAGE_OFFSET_SEQUENCE]);
    }

    return 1U;
}

static uint8_t App_StorageWriteSettings(const AppStorageSettings_t *settings,
                                        uint16_t sequence)
{
    uint8_t record[APP_STORAGE_RECORD_SIZE];
    uint8_t address;

    App_StorageEncodeRecord(settings, sequence, record);
    address = ((sequence & 1U) == 0U) ? APP_STORAGE_SLOT_A_ADDRESS : APP_STORAGE_SLOT_B_ADDRESS;
    return M24C02_Write(address, record, APP_STORAGE_RECORD_SIZE);
}

AppStorageInitResult_t App_StorageInit(void)
{
    AppStorageSettings_t settings;
    uint16_t sequence;

    if (M24C02_IsReady() == 0U)
    {
        s_storageInitialized = 1U;
        s_lastObserved.targetTemperature = 0;
        s_lastObserved.targetSpeed = 0;
        s_lastObserved.targetTime = 0U;
        s_lastSaved = s_lastObserved;
        s_nextSequence = 0U;
        return APP_STORAGE_INIT_ERROR;
    }

    if (App_StorageReadBest(&settings, &sequence) == 0U)
    {
        s_storageInitialized = 1U;
        s_lastObserved.targetTemperature = 0;
        s_lastObserved.targetSpeed = 0;
        s_lastObserved.targetTime = 0U;
        s_lastSaved = s_lastObserved;
        s_nextSequence = 0U;
        return APP_STORAGE_INIT_DEFAULT;
    }

    App_StorageApplySettings(&settings);
    s_lastObserved = settings;
    s_lastSaved = settings;
    s_nextSequence = (uint16_t)(sequence + 1U);
    s_storageInitialized = 1U;
    return APP_STORAGE_INIT_LOADED;
}

uint8_t App_StorageSetSettings(int16_t targetTemperature,
                               int16_t targetSpeed,
                               uint32_t targetTime)
{
    if (targetSpeed < 0)
    {
        targetSpeed = 0;
    }
    else if ((targetSpeed > 0) &&
        (targetSpeed < (int16_t)APP_MOTOR_SPEED_MIN_RPM))
    {
        targetSpeed = (int16_t)APP_MOTOR_SPEED_MIN_RPM;
    }
    else if (targetSpeed > (int16_t)APP_MOTOR_SPEED_LIMIT_RPM)
    {
        targetSpeed = (int16_t)APP_MOTOR_SPEED_LIMIT_RPM;
    }

    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        if (App_SystemLock(portMAX_DELAY) == 0U)
        {
            return 0U;
        }

        g_appData.TargetTemperature = targetTemperature;
        g_appData.TargetSpeed = targetSpeed;
        g_appData.TargetTime = targetTime;
        App_SystemUnlock();
    }
    else
    {
        /* 系统启动阶段没有任务并发，直接更新默认状态。 */
        g_appData.TargetTemperature = targetTemperature;
        g_appData.TargetSpeed = targetSpeed;
        g_appData.TargetTime = targetTime;
    }

    App_StorageRequestSave();
    return 1U;
}

void App_StorageRequestSave(void)
{
    s_saveRequested = 1U;
}

void App_StorageTask(void *argument)
{
    AppStorageSettings_t currentSettings;
    AppStorageSettings_t pendingSettings;
    TickType_t changedTick;
    uint8_t dirty;

    (void)argument;
    dirty = 0U;
    changedTick = xTaskGetTickCount();

    for (;;)
    {
        if ((s_storageInitialized != 0U) && (App_SystemLock(0U) != 0U))
        {
            currentSettings.targetTemperature = g_appData.TargetTemperature;
            currentSettings.targetSpeed = g_appData.TargetSpeed;
            currentSettings.targetTime = g_appData.TargetTime;
            App_SystemUnlock();

            if (App_StorageSettingsEqual(&currentSettings, &s_lastObserved) == 0U)
            {
                s_lastObserved = currentSettings;
                pendingSettings = currentSettings;
                changedTick = xTaskGetTickCount();
                dirty = 1U;
            }

            if (s_saveRequested != 0U)
            {
                pendingSettings = currentSettings;
                changedTick = xTaskGetTickCount();
                dirty = 1U;
                s_saveRequested = 0U;
            }

            if ((dirty != 0U) &&
                ((xTaskGetTickCount() - changedTick) >= pdMS_TO_TICKS(APP_STORAGE_DEBOUNCE_MS)))
            {
                /* EEPROM 写入期间不持有系统状态锁。 */
                if (App_StorageSettingsEqual(&pendingSettings, &s_lastSaved) != 0U)
                {
                    /* 待保存值与 EEPROM 中的值相同，不产生无意义写周期。 */
                    dirty = 0U;
                }
                else if (App_StorageWriteSettings(&pendingSettings, s_nextSequence) != 0U)
                {
                    s_lastSaved = pendingSettings;
                    s_nextSequence++;
                    dirty = 0U;
                }
                else
                {
                    /* 写失败保留 dirty，下一次防抖周期继续尝试。 */
                    changedTick = xTaskGetTickCount();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(APP_STORAGE_TASK_PERIOD_MS));
    }
}

#ifndef APP_STORAGE_H
#define APP_STORAGE_H

#include <stdint.h>

/* EEPROM 用户设置记录的加载结果。 */
typedef enum
{
    APP_STORAGE_INIT_LOADED = 0,
    APP_STORAGE_INIT_DEFAULT,
    APP_STORAGE_INIT_ERROR
} AppStorageInitResult_t;

/* 初始化持久化层并恢复 EEPROM 中的用户设置。 */
AppStorageInitResult_t App_StorageInit(void);

/* 持久化任务入口，负责延迟和防抖写入。 */
void App_StorageTask(void *argument);

/* 在任务上下文中更新三项目标参数，并触发防抖保存。 */
uint8_t App_StorageSetSettings(int16_t targetTemperature,
                               int16_t targetSpeed,
                               uint32_t targetTime);

/* 通知持久化任务重新计算防抖时间，实际写入仍由任务执行。 */
void App_StorageRequestSave(void);

#endif /* APP_STORAGE_H */

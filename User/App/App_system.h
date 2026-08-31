#pragma once

#include <stdint.h>
#include "FreeRTOS.h"

/* STM32F103 的芯片唯一 ID 由 3 个 32 位数据组成，共 96 位。 */
#define APP_UID_WORD_COUNT 3U

/* 电机运行状态，供控制任务、界面任务和故障任务共同使用。 */
typedef enum
{
    APP_MOTOR_STATUS_IDLE = 0,
    APP_MOTOR_STATUS_RUNNING,
    APP_MOTOR_STATUS_FAULT
} AppMotorStatusValue_t;

/* 记录当前电机运行状态。 */
typedef struct
{
    AppMotorStatusValue_t Current;
} AppMotorStatus_t;

/* 用户界面可切换的三个调节焦点。 */
typedef enum
{
    APP_FOCUS_TEMPERATURE = 0,
    APP_FOCUS_SPEED,
    APP_FOCUS_TIME
} AppFocusItem_t;

/* 同时记录当前焦点和上一次焦点，方便界面判断焦点是否发生切换。 */
typedef struct
{
    AppFocusItem_t Current;
    AppFocusItem_t Previous;
} AppFocusState_t;

/* 显示、控制和通信任务共同使用的设备运行数据。 */
typedef struct
{
    int16_t CurrentTemperature;              /* 当前液体温度。 */
    int16_t TargetTemperature;               /* 用户设置的目标温度。 */
    int16_t CurrentSpeed;                    /* 当前电机转速，单位为 RPM。 */
    int16_t TargetSpeed;                     /* 用户设置的目标转速，单位为 RPM。 */
    uint32_t RemainingTime;                  /* 当前剩余的执行时间，单位为秒。 */
    uint32_t TargetTime;                     /* 用户设置的目标时间，单位为秒。 */
    AppMotorStatusValue_t CurrentStatus;     /* 当前空闲、运行或故障状态。 */
    uint32_t Uid[APP_UID_WORD_COUNT];        /* STM32 完整的 96 位芯片唯一 ID。 */
} AppData_t;

/* 三个全局结构体共用同一把互斥锁，访问时必须先调用 App_SystemLock。 */
extern AppMotorStatus_t g_motorStatus;
extern AppFocusState_t g_focusState;
extern AppData_t g_appData;

/* 创建状态互斥锁、设置安全默认值并读取芯片 UID。 */
uint8_t App_SystemInit(void);

/* 仅允许在任务中调用；读取或修改共享结构体前必须先获取互斥锁。 */
uint8_t App_SystemLock(TickType_t waitTicks);
/* 完成共享结构体操作后释放互斥锁。 */
void App_SystemUnlock(void);

/*
 * 在任务上下文中统一修改电机状态，并同步 g_motorStatus 与 g_appData。
 * 当状态首次进入故障时，会自动调用 App_SystemMotorFaultHook。
 */
uint8_t App_SystemSetMotorStatus(AppMotorStatusValue_t status);

/*
 * 周期检查两个共享状态字段；即使其他代码直接写入故障状态，也能触发安全钩子。
 * 后续建议由系统监控任务以固定周期调用，不能在中断中调用。
 */
void App_SystemSafetyCheck(void);

/*
 * 电机故障安全处理预留接口。默认实现为空，后续可在电机模块中重写该函数，
 * 用于停止电机 PWM、关闭驱动使能以及执行其他安全动作。
 */
void App_SystemMotorFaultHook(void);


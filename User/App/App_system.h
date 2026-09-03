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

/* 各模块独立占用故障位，只允许故障所有者在后续恢复流程中清除。 */
typedef enum
{
    APP_FAULT_NONE               = 0U,
    APP_FAULT_STARTUP            = (1U << 0),
    APP_FAULT_IMU_TILT           = (1U << 1),
    APP_FAULT_HEATER_SENSOR      = (1U << 2),
    APP_FAULT_HEATER_OVERTEMP    = (1U << 3),
    APP_FAULT_HEATER_ELECTRICAL  = (1U << 4),
    APP_FAULT_ADC_RUNTIME        = (1U << 5)
} AppFault_t;

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

/* DS3231 当前日历时间，所有字段均为十进制数值。 */
typedef struct
{
    uint16_t Year;
    uint8_t Month;
    uint8_t Date;
    uint8_t Day;
    uint8_t Hours;
    uint8_t Minutes;
    uint8_t Seconds;
} AppRtcTime_t;

/* 显示、控制和通信任务共同使用的设备运行数据。 */
typedef struct
{
    int16_t CurrentTemperature;              /* 当前液体温度。 */
    int16_t CurrentBoardTemperature;         /* 当前板温。 */
    int16_t TargetTemperature;               /* 用户设置的目标温度。 */
    int16_t CurrentSpeed;                    /* 当前电机转速，单位为 RPM。 */
    int16_t TargetSpeed;                     /* 用户设置的目标转速，单位为 RPM。 */
    uint32_t RemainingTime;                  /* 当前剩余的执行时间，单位为秒。 */
    uint32_t TargetTime;                     /* 用户设置的目标时间，单位为秒。 */
    AppMotorStatusValue_t CurrentStatus;     /* 当前空闲、运行或故障状态。 */
    uint32_t Uid[APP_UID_WORD_COUNT];        /* STM32 完整的 96 位芯片唯一 ID。 */
    uint32_t CurrentTime;                    /* 当前已运行时间，单位为秒，从 0 逐秒增加。 */
    uint32_t FaultFlags;                     /* 当前锁存的故障位。 */
    AppRtcTime_t RtcTime;                    /* DS3231 当前日历时间。 */
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

/* 任务上下文故障入口：先关闭全部危险输出，再锁存故障位并进入 FAULT。 */
uint8_t App_SystemSetFault(AppFault_t fault);

/* 仅由对应故障所有者调用，清除自己的故障位并保持输出处于安全状态。 */
uint8_t App_SystemClearFault(AppFault_t fault);

/* 与业务约定一致的故障进入接口，内部复用统一安全停机流程。 */
uint8_t App_SystemEnterFault(AppFault_t fault);

/*
 * 电机故障安全处理预留接口。默认实现为空，后续可在电机模块中重写该函数，
 * 用于停止电机 PWM、关闭驱动使能以及执行其他安全动作。
 */
void App_SystemMotorFaultHook(void);


/*
 * app_freemodbus_port.h
 *
 * 本项目 FreeModbus 端口适配层向应用公开的接口。
 *
 * FreeModbus 核心只通过 mbport.h 中的标准端口函数访问硬件；应用层如需转发
 * UART 回调或读取诊断计数，只能使用本文件声明的方法，不能手写前置声明或
 * 访问 RTU 核心的私有状态。
 */
#ifndef APP_FREEMODBUS_PORT_H
#define APP_FREEMODBUS_PORT_H

#include "mb.h"
#include "usart.h"

/* 由 HAL_UART_RxCpltCallback() 转发 UART4 单字节接收完成事件。 */
void App_FreeModbusSerialRxCpltCallback(UART_HandleTypeDef *huart);

/* 端口层只读诊断计数，用于串口日志，不参与协议状态机控制。 */
ULONG App_FreeModbusSerialGetRxCount(void);
ULONG App_FreeModbusSerialGetRxDropCount(void);
ULONG App_FreeModbusSerialGetTxCount(void);
ULONG App_FreeModbusSerialGetTxFailCount(void);
UCHAR App_FreeModbusSerialGetLastTxByte(void);
ULONG App_FreeModbusTimerGetExpiredCount(void);
ULONG App_FreeModbusEventGetPostCount(void);
ULONG App_FreeModbusEventGetGetCount(void);

#endif /* APP_FREEMODBUS_PORT_H */

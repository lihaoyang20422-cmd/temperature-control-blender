#ifndef APP_MODBUS_H
#define APP_MODBUS_H

#include <stdint.h>
#include "usart.h"

/* 初始化UART4上的Modbus RTU从站。 */
uint8_t App_ModbusInit(void);

/* 创建eMBPoll协议任务和诊断任务。 */
uint8_t App_ModbusCreateTask(void);

/* 由HAL UART接收回调转发UART4单字节接收事件。 */
void App_ModbusRxCpltCallback(UART_HandleTypeDef *huart);

#endif /* APP_MODBUS_H */

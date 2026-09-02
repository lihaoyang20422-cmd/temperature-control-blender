#pragma once

#include <stdint.h>

/* 初始化 ECB01C 串口透传接收。 */
uint8_t App_BluetoothInit(void);

/* 创建蓝牙命令解析任务。 */
uint8_t App_BluetoothCreateTask(void);

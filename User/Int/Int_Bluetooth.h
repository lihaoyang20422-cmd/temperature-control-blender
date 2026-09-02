#pragma once

#include <stdint.h>

/* 初始化 USART2 接收中断。 */
uint8_t Int_BluetoothInit(void);

/* 从 USART2 环形缓冲区读取一个字节，读到返回 1，否则返回 0。 */
uint8_t Int_BluetoothReadByte(uint8_t *data);

/* 在任务上下文中通过 USART2 发送数据。 */
uint8_t Int_BluetoothSend(const uint8_t *data, uint16_t length);

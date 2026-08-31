/*
 * drv_m24c02.h
 * M24C02 EEPROM 阻塞式读写公开接口。
 */
#ifndef DRV_M24C02_H
#define DRV_M24C02_H

#include <stdint.h>

/* M24C02 的 7 bit 基地址为 0x50；STM32 HAL 接口要求左移一位后的地址。 */
#define M24C02_I2C_ADDR_7BIT       0x50U
#define M24C02_I2C_ADDR            (M24C02_I2C_ADDR_7BIT << 1)
#define M24C02_SIZE_BYTES          256U
#define M24C02_PAGE_SIZE_BYTES     16U

/* 在 I2C2 上轮询器件 ACK；就绪返回 1。函数会阻塞，不得从 ISR 调用。 */
uint8_t M24C02_IsReady(void);

/* 从 8 bit 存储地址读取 len 字节；越界或 I2C 失败返回 0。 */
uint8_t M24C02_Read(uint8_t memAddress, uint8_t *data, uint16_t len);

/* 写入 len 字节，自动按 16 字节页拆分并等待每页编程完成；失败返回 0。 */
uint8_t M24C02_Write(uint8_t memAddress, const uint8_t *data, uint16_t len);

#endif /* DRV_M24C02_H */

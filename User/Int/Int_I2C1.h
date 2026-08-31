#ifndef INT_I2C1_H
#define INT_I2C1_H

#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "i2c.h"

/* 创建 I2C1 总线互斥锁，必须在使用 I2C1 的任务创建前调用。 */
uint8_t Bsp_I2c1Init(void);

/* 以下接口在一次完整 HAL I2C 事务期间持有总线锁，禁止在中断中调用。 */
HAL_StatusTypeDef Bsp_I2c1MasterTransmit(uint16_t deviceAddress,
                                         const uint8_t *data,
                                         uint16_t size,
                                         uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c1MemWrite(uint16_t deviceAddress,
                                   uint16_t memoryAddress,
                                   uint16_t memoryAddressSize,
                                   const uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c1MemRead(uint16_t deviceAddress,
                                  uint16_t memoryAddress,
                                  uint16_t memoryAddressSize,
                                  uint8_t *data,
                                  uint16_t size,
                                  uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c1IsDeviceReady(uint16_t deviceAddress,
                                        uint32_t trials,
                                        uint32_t timeoutMs);

#endif /* INT_I2C1_H */

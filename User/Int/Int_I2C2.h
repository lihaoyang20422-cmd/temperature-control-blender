#ifndef INT_I2C2_H
#define INT_I2C2_H

#include <stdint.h>
#include "i2c.h"

/*
 * 创建 I2C2 总线互斥锁。
 * 必须在 MX_I2C2_Init() 之后、创建使用 I2C2 的任务之前调用。
 */
uint8_t Bsp_I2c2Init(void);

/*
 * 以下接口会在一次完整的 HAL I2C 事务期间持有总线互斥锁。
 * 接口采用阻塞方式，只能在任务或调度器启动前调用，禁止在中断中调用。
 */
HAL_StatusTypeDef Bsp_I2c2MasterTransmit(uint16_t deviceAddress,
                                         const uint8_t *data,
                                         uint16_t size,
                                         uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c2MasterReceive(uint16_t deviceAddress,
                                        uint8_t *data,
                                        uint16_t size,
                                        uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c2MemWrite(uint16_t deviceAddress,
                                   uint16_t memoryAddress,
                                   uint16_t memoryAddressSize,
                                   const uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c2MemRead(uint16_t deviceAddress,
                                  uint16_t memoryAddress,
                                  uint16_t memoryAddressSize,
                                  uint8_t *data,
                                  uint16_t size,
                                  uint32_t timeoutMs);
HAL_StatusTypeDef Bsp_I2c2IsDeviceReady(uint16_t deviceAddress,
                                        uint32_t trials,
                                        uint32_t timeoutMs);

#endif /* INT_I2C2_H */

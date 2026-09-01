#ifndef INT_SPI2_H
#define INT_SPI2_H

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* 创建 SPI2 总线互斥锁；必须在任何 SPI2 设备访问前调用。 */
uint8_t Bsp_Spi2Init(void);

/* SPI2 全双工事务，内部负责总线锁和 LSM6DSM 片选时序。 */
HAL_StatusTypeDef Bsp_Spi2TransmitReceive(const uint8_t *txData,
                                          uint8_t *rxData,
                                          uint16_t size,
                                          uint32_t timeoutMs);

/* SPI2 只发送事务，适用于寄存器写操作。 */
HAL_StatusTypeDef Bsp_Spi2Transmit(const uint8_t *txData,
                                   uint16_t size,
                                   uint32_t timeoutMs);

#endif /* INT_SPI2_H */


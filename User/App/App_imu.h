#ifndef APP_IMU_H
#define APP_IMU_H

#include <stdint.h>

typedef struct
{
    int16_t x;
    int16_t y;
    int16_t z;
} AppImuAccelRaw_t;

/* LSM6DSM 的 SPI 寄存器访问和上电通信验证。 */
uint8_t App_ImuInit(void);
uint8_t App_ImuReadRegister(uint8_t registerAddress, uint8_t *value);
uint8_t App_ImuWriteRegister(uint8_t registerAddress, uint8_t value);
uint8_t App_ImuReadRegisters(uint8_t startAddress, uint8_t *buffer, uint16_t size);
uint8_t App_ImuCreateTask(void);
void App_ImuExtiCallback(uint16_t GPIO_Pin);
uint8_t App_ImuIsTilted(const AppImuAccelRaw_t *accel);
uint8_t App_ImuGetAccel(AppImuAccelRaw_t *accel);

#endif /* APP_IMU_H */

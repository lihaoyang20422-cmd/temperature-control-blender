#include "Int_I2C1.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define BSP_I2C1_LOCK_FAILED       0U
#define BSP_I2C1_LOCK_ACQUIRED     1U
#define BSP_I2C1_LOCK_NOT_REQUIRED 2U

/* I2C1 总线互斥锁，OLED 等所有 I2C1 设备都必须通过本文件访问。 */
static SemaphoreHandle_t s_i2c1Mutex = NULL;

static uint8_t Bsp_I2c1Lock(void)
{
    BaseType_t schedulerState;

    /* 阻塞式互斥锁和 HAL 轮询事务不能在中断服务函数中使用。 */
    if (__get_IPSR() != 0U)
    {
        return BSP_I2C1_LOCK_FAILED;
    }

    if (s_i2c1Mutex == NULL)
    {
        return BSP_I2C1_LOCK_FAILED;
    }

    schedulerState = xTaskGetSchedulerState();
    if (schedulerState == taskSCHEDULER_NOT_STARTED)
    {
        /* 调度器启动前没有任务并发访问，可以直接执行事务。 */
        return BSP_I2C1_LOCK_NOT_REQUIRED;
    }

    if (schedulerState != taskSCHEDULER_RUNNING)
    {
        return BSP_I2C1_LOCK_FAILED;
    }

    return (xSemaphoreTake(s_i2c1Mutex, portMAX_DELAY) == pdTRUE) ?
           BSP_I2C1_LOCK_ACQUIRED : BSP_I2C1_LOCK_FAILED;
}

static void Bsp_I2c1Unlock(uint8_t lockState)
{
    if (lockState == BSP_I2C1_LOCK_ACQUIRED)
    {
        (void)xSemaphoreGive(s_i2c1Mutex);
    }
}

uint8_t Bsp_I2c1Init(void)
{
    if (s_i2c1Mutex != NULL)
    {
        return 1U;
    }

    /* 防止 CubeMX 尚未初始化 I2C1 时误创建总线服务。 */
    if (hi2c1.Instance != I2C1)
    {
        return 0U;
    }

    s_i2c1Mutex = xSemaphoreCreateMutex();
    return (s_i2c1Mutex != NULL) ? 1U : 0U;
}

HAL_StatusTypeDef Bsp_I2c1MasterTransmit(uint16_t deviceAddress,
                                         const uint8_t *data,
                                         uint16_t size,
                                         uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if ((data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lockState = Bsp_I2c1Lock();
    if (lockState == BSP_I2C1_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Master_Transmit(&hi2c1, deviceAddress, (uint8_t *)data, size, timeoutMs);
    Bsp_I2c1Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c1MemWrite(uint16_t deviceAddress,
                                   uint16_t memoryAddress,
                                   uint16_t memoryAddressSize,
                                   const uint8_t *data,
                                   uint16_t size,
                                   uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if ((data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lockState = Bsp_I2c1Lock();
    if (lockState == BSP_I2C1_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Mem_Write(&hi2c1, deviceAddress, memoryAddress,
                               memoryAddressSize, (uint8_t *)data, size, timeoutMs);
    Bsp_I2c1Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c1MemRead(uint16_t deviceAddress,
                                  uint16_t memoryAddress,
                                  uint16_t memoryAddressSize,
                                  uint8_t *data,
                                  uint16_t size,
                                  uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if ((data == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }

    lockState = Bsp_I2c1Lock();
    if (lockState == BSP_I2C1_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Mem_Read(&hi2c1, deviceAddress, memoryAddress,
                              memoryAddressSize, data, size, timeoutMs);
    Bsp_I2c1Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c1IsDeviceReady(uint16_t deviceAddress,
                                        uint32_t trials,
                                        uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if (trials == 0U)
    {
        return HAL_ERROR;
    }

    lockState = Bsp_I2c1Lock();
    if (lockState == BSP_I2C1_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_IsDeviceReady(&hi2c1, deviceAddress, trials, timeoutMs);
    Bsp_I2c1Unlock(lockState);
    return status;
}

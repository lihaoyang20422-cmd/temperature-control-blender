#include "Int_I2C2.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define BSP_I2C2_LOCK_FAILED 0U
#define BSP_I2C2_LOCK_ACQUIRED 1U
#define BSP_I2C2_LOCK_NOT_REQUIRED 2U

/* I2C2 独占互斥锁，只允许通过本文件公开接口访问。 */
static SemaphoreHandle_t s_i2c2Mutex;

/* 获取一次 I2C2 总线使用权；等待期间当前任务会进入阻塞态，不会空转占用 CPU。 */
static uint8_t Bsp_I2c2Lock(void)
{
    BaseType_t schedulerState;

    /* 阻塞式 HAL 和普通互斥锁均不能在中断服务函数中使用。 */
    if (__get_IPSR() != 0U)
    {
        return BSP_I2C2_LOCK_FAILED;
    }

    if (s_i2c2Mutex == NULL)
    {
        return BSP_I2C2_LOCK_FAILED;
    }

    schedulerState = xTaskGetSchedulerState();
    if (schedulerState == taskSCHEDULER_NOT_STARTED)
    {
        /* 调度器启动前不存在任务抢占，直接执行 I2C 事务即可。 */
        return BSP_I2C2_LOCK_NOT_REQUIRED;
    }

    if (schedulerState != taskSCHEDULER_RUNNING)
    {
        /* 调度器挂起期间禁止进入可能阻塞的 I2C 事务。 */
        return BSP_I2C2_LOCK_FAILED;
    }

    return (xSemaphoreTake(s_i2c2Mutex, portMAX_DELAY) == pdTRUE) ? BSP_I2C2_LOCK_ACQUIRED : BSP_I2C2_LOCK_FAILED;
}

/* 一次完整 I2C 事务结束后立即释放总线，让其他任务继续访问。 */
static void Bsp_I2c2Unlock(uint8_t lockState)
{
    if (lockState == BSP_I2C2_LOCK_ACQUIRED)
    {
        (void)xSemaphoreGive(s_i2c2Mutex);
    }
}

uint8_t Bsp_I2c2Init(void)
{
    if (s_i2c2Mutex != NULL)
    {
        return 1U;
    }

    /* 防止在 CubeMX 尚未初始化 I2C2 外设时提前创建总线服务。 */
    if (hi2c2.Instance != I2C2)
    {
        return 0U;
    }

    s_i2c2Mutex = xSemaphoreCreateMutex();
    return (s_i2c2Mutex != NULL) ? 1U : 0U;
}

HAL_StatusTypeDef Bsp_I2c2MasterTransmit(uint16_t deviceAddress,
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

    lockState = Bsp_I2c2Lock();
    if (lockState == BSP_I2C2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Master_Transmit(&hi2c2, deviceAddress, (uint8_t *)data, size, timeoutMs);
    Bsp_I2c2Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c2MasterReceive(uint16_t deviceAddress,
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

    lockState = Bsp_I2c2Lock();
    if (lockState == BSP_I2C2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Master_Receive(&hi2c2, deviceAddress, data, size, timeoutMs);
    Bsp_I2c2Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c2MemWrite(uint16_t deviceAddress,
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

    lockState = Bsp_I2c2Lock();
    if (lockState == BSP_I2C2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Mem_Write(&hi2c2,
                               deviceAddress,
                               memoryAddress,
                               memoryAddressSize,
                               (uint8_t *)data,
                               size,
                               timeoutMs);
    Bsp_I2c2Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c2MemRead(uint16_t deviceAddress,
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

    lockState = Bsp_I2c2Lock();
    if (lockState == BSP_I2C2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_Mem_Read(&hi2c2,
                              deviceAddress,
                              memoryAddress,
                              memoryAddressSize,
                              data,
                              size,
                              timeoutMs);
    Bsp_I2c2Unlock(lockState);
    return status;
}

HAL_StatusTypeDef Bsp_I2c2IsDeviceReady(uint16_t deviceAddress,
                                        uint32_t trials,
                                        uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if (trials == 0U)
    {
        return HAL_ERROR;
    }

    lockState = Bsp_I2c2Lock();
    if (lockState == BSP_I2C2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    status = HAL_I2C_IsDeviceReady(&hi2c2, deviceAddress, trials, timeoutMs);
    Bsp_I2c2Unlock(lockState);
    return status;
}

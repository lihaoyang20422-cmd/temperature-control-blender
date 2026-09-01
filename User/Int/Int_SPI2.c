#include "Int_SPI2.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "spi.h"
#include "bsp_pins.h"

#define BSP_SPI2_LOCK_FAILED       0U
#define BSP_SPI2_LOCK_ACQUIRED     1U
#define BSP_SPI2_LOCK_NOT_REQUIRED 2U

/* SPI2 当前只有 LSM6DSM 使用，但仍统一通过互斥锁保护总线。 */
static SemaphoreHandle_t s_spi2Mutex;

static uint8_t Bsp_Spi2Lock(void)
{
    BaseType_t schedulerState;

    /* 普通互斥锁不能在中断服务函数中阻塞等待。 */
    if (__get_IPSR() != 0U)
    {
        return BSP_SPI2_LOCK_FAILED;
    }
    if (s_spi2Mutex == NULL)
    {
        return BSP_SPI2_LOCK_FAILED;
    }

    schedulerState = xTaskGetSchedulerState();
    if (schedulerState == taskSCHEDULER_NOT_STARTED)
    {
        /* 调度器启动前没有并发任务，可以直接访问 SPI。 */
        return BSP_SPI2_LOCK_NOT_REQUIRED;
    }
    if (schedulerState != taskSCHEDULER_RUNNING)
    {
        return BSP_SPI2_LOCK_FAILED;
    }
    return (xSemaphoreTake(s_spi2Mutex, portMAX_DELAY) == pdTRUE) ?
           BSP_SPI2_LOCK_ACQUIRED : BSP_SPI2_LOCK_FAILED;
}

static void Bsp_Spi2Unlock(uint8_t lockState)
{
    if (lockState == BSP_SPI2_LOCK_ACQUIRED)
    {
        (void)xSemaphoreGive(s_spi2Mutex);
    }
}

static HAL_StatusTypeDef Bsp_Spi2Exchange(const uint8_t *txData,
                                          uint8_t *rxData,
                                          uint16_t size,
                                          uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if ((txData == NULL) || (rxData == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }
    lockState = Bsp_Spi2Lock();
    if (lockState == BSP_SPI2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    /* CS 必须覆盖完整的“地址 + 数据”事务，不能中途释放。 */
    HAL_GPIO_WritePin(LSM_CS_GPIO_Port, LSM_CS_Pin, LSM_CS_ACTIVE_STATE);
    status = HAL_SPI_TransmitReceive(&hspi2, (uint8_t *)txData, rxData,
                                     size, timeoutMs);
    HAL_GPIO_WritePin(LSM_CS_GPIO_Port, LSM_CS_Pin, LSM_CS_INACTIVE_STATE);
    Bsp_Spi2Unlock(lockState);
    return status;
}

uint8_t Bsp_Spi2Init(void)
{
    if (s_spi2Mutex != NULL)
    {
        return 1U;
    }
    if (hspi2.Instance != SPI2)
    {
        return 0U;
    }

    /* 确保初始化完成前传感器处于未选中状态。 */
    HAL_GPIO_WritePin(LSM_CS_GPIO_Port, LSM_CS_Pin, LSM_CS_INACTIVE_STATE);
    s_spi2Mutex = xSemaphoreCreateMutex();
    return (s_spi2Mutex != NULL) ? 1U : 0U;
}

HAL_StatusTypeDef Bsp_Spi2TransmitReceive(const uint8_t *txData,
                                          uint8_t *rxData,
                                          uint16_t size,
                                          uint32_t timeoutMs)
{
    return Bsp_Spi2Exchange(txData, rxData, size, timeoutMs);
}

HAL_StatusTypeDef Bsp_Spi2Transmit(const uint8_t *txData,
                                   uint16_t size,
                                   uint32_t timeoutMs)
{
    HAL_StatusTypeDef status;
    uint8_t lockState;

    if ((txData == NULL) || (size == 0U))
    {
        return HAL_ERROR;
    }
    lockState = Bsp_Spi2Lock();
    if (lockState == BSP_SPI2_LOCK_FAILED)
    {
        return HAL_BUSY;
    }

    /* 写寄存器时地址和数据仍属于同一片选事务。 */
    HAL_GPIO_WritePin(LSM_CS_GPIO_Port, LSM_CS_Pin, LSM_CS_ACTIVE_STATE);
    status = HAL_SPI_Transmit(&hspi2, (uint8_t *)txData, size, timeoutMs);
    HAL_GPIO_WritePin(LSM_CS_GPIO_Port, LSM_CS_Pin, LSM_CS_INACTIVE_STATE);
    Bsp_Spi2Unlock(lockState);
    return status;
}

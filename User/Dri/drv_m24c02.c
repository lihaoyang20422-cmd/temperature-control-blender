/*
 * drv_m24c02.c
 *
 * 基于 bsp_i2c2 公开接口的 M24C02 EEPROM 驱动。
 * 负责 256 字节地址范围检查、16 字节页边界拆分和写周期 ACK 确认；I2C2
 * 的跨任务互斥由 BSP 层统一处理，本层不直接访问 HAL I2C 句柄。
 */
#include "drv_m24c02.h"
#include "Int_i2c2.h"
#include "FreeRTOS.h"
#include "task.h"

#define M24C02_TIMEOUT_MS          50U
#define M24C02_READY_TRIALS        20U
#define M24C02_WRITE_CYCLE_MS      5U

/* 调度器运行后使用任务延时；启动阶段使用基于 TIM6 时基的 HAL 延时。 */
static void M24C02_DelayMs(uint32_t ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(ms));
    }
    else
    {
        HAL_Delay(ms);
    }
}

uint8_t M24C02_IsReady(void)
{
    return (Bsp_I2c2IsDeviceReady(M24C02_I2C_ADDR, M24C02_READY_TRIALS, M24C02_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

uint8_t M24C02_Read(uint8_t memAddress, uint8_t *data, uint16_t len)
{
    /* 在发起 I2C 事务前检查 8 bit 地址范围，防止无意回绕覆盖前部数据。 */
    if ((data == NULL) || (((uint16_t)memAddress + len) > M24C02_SIZE_BYTES))
    {
        return 0U;
    }

    return (Bsp_I2c2MemRead(M24C02_I2C_ADDR,
                            memAddress,
                            I2C_MEMADD_SIZE_8BIT,
                            data,
                            len,
                            M24C02_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

uint8_t M24C02_Write(uint8_t memAddress, const uint8_t *data, uint16_t len)
{
    uint16_t remain;
    uint16_t chunk;
    uint16_t pageRemain;
    uint16_t offset;

    if ((data == NULL) || (((uint16_t)memAddress + len) > M24C02_SIZE_BYTES))
    {
        return 0U;
    }

    remain = len;
    offset = 0;

    while (remain > 0U)
    {
        /* M24C02 单次页写不能跨越 16 字节边界，否则芯片会在当前页内回绕。 */
        pageRemain = M24C02_PAGE_SIZE_BYTES - (((uint16_t)memAddress + offset) % M24C02_PAGE_SIZE_BYTES);
        chunk = (remain < pageRemain) ? remain : pageRemain;

        if (Bsp_I2c2MemWrite(M24C02_I2C_ADDR,
                             (uint8_t)(memAddress + offset),
                             I2C_MEMADD_SIZE_8BIT,
                             &data[offset],
                             chunk,
                             M24C02_TIMEOUT_MS) != HAL_OK)
        {
            return 0U;
        }

        /* 等待内部编程周期并轮询 ACK，确认本页落盘后才继续下一页。 */
        M24C02_DelayMs(M24C02_WRITE_CYCLE_MS);

        if (M24C02_IsReady() == 0U)
        {
            return 0U;
        }

        offset += chunk;
        remain -= chunk;
    }

    return 1U;
}

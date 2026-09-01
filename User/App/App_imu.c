#include "App_imu.h"
#include "Com_debug.h"
#include "Int_SPI2.h"
#include "App_system.h"
#include "bsp_pins.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdlib.h>

#define LSM6DSM_REG_WHO_AM_I    0x0FU
#define LSM6DSM_WHO_AM_I_VALUE  0x6AU
#define LSM6DSM_SPI_READ_BIT    0x80U
#define LSM6DSM_SPI_WRITE_MASK  0x7FU
#define LSM6DSM_SPI_MAX_BURST   32U
#define LSM6DSM_SPI_TIMEOUT_MS  100U
#define LSM6DSM_REG_CTRL1_XL    0x10U
#define LSM6DSM_REG_CTRL3_C     0x12U
#define LSM6DSM_REG_INT1_CTRL   0x0DU
#define LSM6DSM_REG_OUTX_L_XL   0x28U

/* 倾倒判定使用 20 ms 采样周期，连续计数形成抗抖动滞回。 */
#define IMU_TILT_ENTER_COUNT    5U
#define IMU_TILT_EXIT_COUNT     20U
#define IMU_TILT_TASK_PERIOD_MS 20U
/* ±2 g 量程下 1 g 约为 16384 LSB，进入阈值约为 0.70 g。 */
#define IMU_TILT_ENTER_AXIS_RAW 11468
#define IMU_TILT_EXIT_Z_RAW     13107
#define IMU_TILT_EXIT_XY_RAW    9830

static TaskHandle_t s_imuTaskHandle;
static AppImuAccelRaw_t s_accelRaw;
static uint8_t s_accelValid;
static uint8_t s_tilted;
static uint8_t s_tiltEnterCount;
static uint8_t s_tiltExitCount;

static uint8_t App_ImuReadAccel(AppImuAccelRaw_t *accel)
{
    uint8_t data[6];

    if ((accel == NULL) || (App_ImuReadRegisters(LSM6DSM_REG_OUTX_L_XL, data, 6U) == 0U))
    {
        return 0U;
    }
    accel->x = (int16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    accel->y = (int16_t)((uint16_t)data[2] | ((uint16_t)data[3] << 8));
    accel->z = (int16_t)((uint16_t)data[4] | ((uint16_t)data[5] << 8));
    return 1U;
}

uint8_t App_ImuIsTilted(const AppImuAccelRaw_t *accel)
{
    int32_t x;
    int32_t y;
    int32_t z;

    if (accel == NULL)
    {
        return 0U;
    }
    x = labs((long)accel->x);
    y = labs((long)accel->y);
    z = labs((long)accel->z);
    /* 设备竖直时重力主要在 Z 轴；Z 下降或 X/Y 增大表示可能倾倒。 */
    return ((z < IMU_TILT_ENTER_AXIS_RAW) ||
            (x > IMU_TILT_ENTER_AXIS_RAW) ||
            (y > IMU_TILT_ENTER_AXIS_RAW)) ? 1U : 0U;
}

static uint8_t App_ImuIsNormal(const AppImuAccelRaw_t *accel)
{
    int32_t x;
    int32_t y;
    int32_t z;

    if (accel == NULL)
    {
        return 0U;
    }
    x = labs((long)accel->x);
    y = labs((long)accel->y);
    z = labs((long)accel->z);
    /* 退出阈值采用滞回，避免设备在临界角度附近反复进出故障。 */
    return ((z >= IMU_TILT_EXIT_Z_RAW) &&
            (x <= IMU_TILT_EXIT_XY_RAW) &&
            (y <= IMU_TILT_EXIT_XY_RAW)) ? 1U : 0U;
}

uint8_t App_ImuGetAccel(AppImuAccelRaw_t *accel)
{
    if ((accel == NULL) || (s_accelValid == 0U))
    {
        return 0U;
    }
    *accel = s_accelRaw;
    return 1U;
}

static void App_ImuTask(void *argument)
{
    AppImuAccelRaw_t accel;
    uint8_t tilted;

    (void)argument;
    for (;;)
    {
        /* 中断通知用于提前采样，超时则保证中断未接通时也会周期采样。 */
        (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(IMU_TILT_TASK_PERIOD_MS));
        if (App_ImuReadAccel(&accel) == 0U)
        {
            continue;
        }
        s_accelRaw = accel;
        s_accelValid = 1U;
        tilted = App_ImuIsTilted(&accel);

        if (tilted != 0U)
        {
            s_tiltExitCount = 0U;
            if (s_tiltEnterCount < IMU_TILT_ENTER_COUNT)
            {
                s_tiltEnterCount++;
            }
            if ((s_tilted == 0U) && (s_tiltEnterCount >= IMU_TILT_ENTER_COUNT))
            {
                s_tilted = 1U;
                debug_printfln("IMU tilt detected, enter fault");
                (void)App_SystemEnterFault(APP_FAULT_IMU_TILT);
            }
        }
        else
        {
            s_tiltEnterCount = 0U;
            if (App_ImuIsNormal(&accel) != 0U)
            {
                if (s_tiltExitCount < IMU_TILT_EXIT_COUNT)
                {
                    s_tiltExitCount++;
                }
                if ((s_tilted != 0U) && (s_tiltExitCount >= IMU_TILT_EXIT_COUNT))
                {
                    s_tilted = 0U;
                    debug_printfln("IMU tilt cleared, return to idle");
                    (void)App_SystemClearFault(APP_FAULT_IMU_TILT);
                }
            }
            else
            {
                s_tiltExitCount = 0U;
            }
        }
    }
}

uint8_t App_ImuCreateTask(void)
{
    if (s_imuTaskHandle != NULL)
    {
        return 1U;
    }
    return (xTaskCreate(App_ImuTask, "IMU", 256U, NULL,
                        tskIDLE_PRIORITY + 2U, &s_imuTaskHandle) == pdPASS) ?
           1U : 0U;
}

void App_ImuExtiCallback(uint16_t GPIO_Pin)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    /* ISR 只通知任务，禁止在此处读 SPI、打印、加锁或执行故障停机。 */
    if ((s_imuTaskHandle != NULL) &&
        ((GPIO_Pin == LSM_INT1_Pin) || (GPIO_Pin == LSM_INT2_Pin)))
    {
        (void)vTaskNotifyGiveFromISR(s_imuTaskHandle, &higherPriorityTaskWoken);
        portYIELD_FROM_ISR(higherPriorityTaskWoken);
    }
}

uint8_t App_ImuReadRegister(uint8_t registerAddress, uint8_t *value)
{
    uint8_t txData[2];
    uint8_t rxData[2];

    if (value == NULL)
    {
        return 0U;
    }
    txData[0] = (uint8_t)(registerAddress | LSM6DSM_SPI_READ_BIT);
    txData[1] = 0xFFU;
    if (Bsp_Spi2TransmitReceive(txData, rxData, 2U, LSM6DSM_SPI_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }
    *value = rxData[1];
    return 1U;
}

uint8_t App_ImuWriteRegister(uint8_t registerAddress, uint8_t value)
{
    uint8_t txData[2];

    txData[0] = (uint8_t)(registerAddress & LSM6DSM_SPI_WRITE_MASK);
    txData[1] = value;
    return (Bsp_Spi2Transmit(txData, 2U, LSM6DSM_SPI_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

uint8_t App_ImuReadRegisters(uint8_t startAddress, uint8_t *buffer, uint16_t size)
{
    uint8_t txData[LSM6DSM_SPI_MAX_BURST + 1U];
    uint8_t rxData[LSM6DSM_SPI_MAX_BURST + 1U];
    uint16_t index;

    if ((buffer == NULL) || (size == 0U) || (size > LSM6DSM_SPI_MAX_BURST))
    {
        return 0U;
    }
    /* 多字节读取依赖 CTRL3_C 的地址自动递增配置。 */
    txData[0] = (uint8_t)(startAddress | LSM6DSM_SPI_READ_BIT);
    for (index = 1U; index <= size; index++)
    {
        txData[index] = 0xFFU;
    }
    if (Bsp_Spi2TransmitReceive(txData, rxData, (uint16_t)(size + 1U),
                                LSM6DSM_SPI_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }
    for (index = 0U; index < size; index++)
    {
        buffer[index] = rxData[index + 1U];
    }
    return 1U;
}

uint8_t App_ImuInit(void)
{
    uint8_t whoAmI;

    if (Bsp_Spi2Init() == 0U)
    {
        debug_printfln("LSM6DSM SPI2 init failed");
        return 0U;
    }
    /* WHO_AM_I 是传感器通信、片选、供电和型号的第一道验证。 */
    if (App_ImuReadRegister(LSM6DSM_REG_WHO_AM_I, &whoAmI) == 0U)
    {
        debug_printfln("LSM6DSM WHO_AM_I read failed");
        return 0U;
    }
    debug_printf("LSM6DSM WHO_AM_I = 0x%02X\r\n", whoAmI);
    if (whoAmI != LSM6DSM_WHO_AM_I_VALUE)
    {
        debug_printfln("LSM6DSM WHO_AM_I mismatch");
        return 0U;
    }
    /* 104 Hz、±2 g；启用 BDU、地址自动递增，并将加速度数据就绪映射到 INT1。 */
    if ((App_ImuWriteRegister(LSM6DSM_REG_CTRL3_C, 0x44U) == 0U) ||
        (App_ImuWriteRegister(LSM6DSM_REG_CTRL1_XL, 0x40U) == 0U) ||
        (App_ImuWriteRegister(LSM6DSM_REG_INT1_CTRL, 0x01U) == 0U))
    {
        debug_printfln("LSM6DSM accelerometer config failed");
        return 0U;
    }
    debug_printfln("LSM6DSM ready");
    return 1U;
}

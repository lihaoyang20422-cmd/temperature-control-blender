#include "Dri_ds3231.h"
#include "Int_I2C2.h"

#define DRI_DS3231_REG_SECONDS       0x00U
#define DRI_DS3231_REG_STATUS        0x0FU
#define DRI_DS3231_TIMEOUT_MS        50U

static uint8_t Dri_Ds3231BcdToBin(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint8_t Dri_Ds3231BinToBcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint8_t Dri_Ds3231IsValidTime(const DriDs3231Time_t *time)
{
    if (time == NULL)
    {
        return 0U;
    }

    return ((time->year >= 2000U) && (time->year <= 2099U) &&
            (time->month >= 1U) && (time->month <= 12U) &&
            (time->date >= 1U) && (time->date <= 31U) &&
            (time->day >= 1U) && (time->day <= 7U) &&
            (time->hours < 24U) && (time->minutes < 60U) &&
            (time->seconds < 60U)) ? 1U : 0U;
}

uint8_t Dri_Ds3231ReadTime(DriDs3231Time_t *time)
{
    uint8_t data[7];
    uint8_t hour;

    if ((time == NULL) ||
        (Bsp_I2c2MemRead(DRI_DS3231_I2C_ADDRESS,
                         DRI_DS3231_REG_SECONDS,
                         I2C_MEMADD_SIZE_8BIT, data, sizeof(data),
                         DRI_DS3231_TIMEOUT_MS) != HAL_OK))
    {
        return 0U;
    }

    time->seconds = Dri_Ds3231BcdToBin(data[0] & 0x7FU);
    time->minutes = Dri_Ds3231BcdToBin(data[1] & 0x7FU);
    hour = data[2];
    if ((hour & 0x40U) != 0U)
    {
        time->hours = Dri_Ds3231BcdToBin(hour & 0x1FU);
        if (((hour & 0x20U) != 0U) && (time->hours < 12U))
        {
            time->hours += 12U;
        }
        else if (((hour & 0x20U) == 0U) && (time->hours == 12U))
        {
            time->hours = 0U;
        }
    }
    else
    {
        time->hours = Dri_Ds3231BcdToBin(hour & 0x3FU);
    }
    time->day = Dri_Ds3231BcdToBin(data[3] & 0x07U);
    time->date = Dri_Ds3231BcdToBin(data[4] & 0x3FU);
    time->month = Dri_Ds3231BcdToBin(data[5] & 0x1FU);
    time->year = (uint16_t)(2000U + Dri_Ds3231BcdToBin(data[6]));

    return ((time->seconds < 60U) && (time->minutes < 60U) &&
            (time->hours < 24U) && (time->day >= 1U) && (time->day <= 7U) &&
            (time->date >= 1U) && (time->date <= 31U) &&
            (time->month >= 1U) && (time->month <= 12U)) ? 1U : 0U;
}

uint8_t Dri_Ds3231Init(void)
{
    return (Bsp_I2c2IsDeviceReady(DRI_DS3231_I2C_ADDRESS, 3U,
                                  DRI_DS3231_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

uint8_t Dri_Ds3231WriteTime(const DriDs3231Time_t *time)
{
    uint8_t data[7];
    uint8_t status;

    if (Dri_Ds3231IsValidTime(time) == 0U)
    {
        return 0U;
    }

    data[0] = Dri_Ds3231BinToBcd(time->seconds);
    data[1] = Dri_Ds3231BinToBcd(time->minutes);
    data[2] = Dri_Ds3231BinToBcd(time->hours);
    data[3] = Dri_Ds3231BinToBcd(time->day);
    data[4] = Dri_Ds3231BinToBcd(time->date);
    data[5] = Dri_Ds3231BinToBcd(time->month);
    data[6] = Dri_Ds3231BinToBcd((uint8_t)(time->year - 2000U));

    /* 一次连续写入秒到年的 7 个寄存器，访问由 I2C2 BSP 统一加锁。 */
    if (Bsp_I2c2MemWrite(DRI_DS3231_I2C_ADDRESS, DRI_DS3231_REG_SECONDS,
                        I2C_MEMADD_SIZE_8BIT, data, sizeof(data),
                        DRI_DS3231_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }

    /* 清除 OSF 位，表示本次写入后时钟已经完成校准。 */
    if (Bsp_I2c2MemRead(DRI_DS3231_I2C_ADDRESS, DRI_DS3231_REG_STATUS,
                       I2C_MEMADD_SIZE_8BIT, &status, 1U,
                       DRI_DS3231_TIMEOUT_MS) != HAL_OK)
    {
        return 0U;
    }
    status &= (uint8_t)~0x80U;
    return (Bsp_I2c2MemWrite(DRI_DS3231_I2C_ADDRESS, DRI_DS3231_REG_STATUS,
                             I2C_MEMADD_SIZE_8BIT, &status, 1U,
                             DRI_DS3231_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

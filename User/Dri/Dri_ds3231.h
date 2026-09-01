#ifndef DRI_DS3231_H
#define DRI_DS3231_H

#include <stdint.h>

/* DS3231 的 7 位 I2C 地址为 0x68，HAL 接口使用左移后的地址。 */
#define DRI_DS3231_I2C_ADDRESS       (0x68U << 1)

typedef struct
{
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t date;
    uint8_t month;
    uint16_t year;
} DriDs3231Time_t;

/* 探测 DS3231 并读取当前时间，成功返回 1。 */
uint8_t Dri_Ds3231Init(void);
uint8_t Dri_Ds3231ReadTime(DriDs3231Time_t *time);

#endif

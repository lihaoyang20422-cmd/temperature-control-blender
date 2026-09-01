#ifndef APP_RTC_H
#define APP_RTC_H

#include <stdint.h>

/* 初始化 DS3231 并读取一次时间，用于确认外部 RTC 在线。 */
uint8_t App_RtcInit(void);

#endif

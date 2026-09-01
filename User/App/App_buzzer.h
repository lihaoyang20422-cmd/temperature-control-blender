#pragma once

#include <stdint.h>

/* TIM1_CH1 / PA8：4 kHz 载波，较低占空比用于温和按键提示音。 */
#define APP_BUZZER_SHORT_MS 50U

uint8_t App_BuzzerInit(void);
void App_BuzzerBeepShort(void);
void App_BuzzerSetContinuous(uint8_t enable);

/* 在 FreeRTOS 调度器启动前播放一次开机提示音。 */
void App_BuzzerStartupSound(void);

/* Backward-compatible spelling used by older application code. */
#define App_Buzzer_Init App_BuzzerInit


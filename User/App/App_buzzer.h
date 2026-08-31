#pragma once

#include <stdint.h>

/* TIM1_CH1 / PA8: 4 kHz carrier, 50% duty cycle. */
#define APP_BUZZER_SHORT_MS 100U

uint8_t App_BuzzerInit(void);
void App_BuzzerBeepShort(void);
void App_BuzzerSetContinuous(uint8_t enable);

/* Backward-compatible spelling used by older application code. */
#define App_Buzzer_Init App_BuzzerInit


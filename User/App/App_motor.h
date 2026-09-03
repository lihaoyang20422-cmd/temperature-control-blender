#pragma once

#include <stdint.h>

/* 初始化 TIM3 编码器和 TIM4 搅拌电机 PWM，默认保持电机停止。 */
uint8_t App_MotorInit(void);

/* 停止 TIM4_CH3 PWM 并清零比较值。 */
void App_MotorStop(void);

/* 创建电机闭环控制任务。 */
uint8_t App_MotorCreateTask(void);


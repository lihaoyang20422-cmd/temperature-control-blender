#pragma once

#include <stdint.h>

/* 全部控制入口允许的最高目标转速，防止机械系统超过安全转速。 */
#define APP_MOTOR_SPEED_MIN_RPM   200U
#define APP_MOTOR_SPEED_LIMIT_RPM 1000U

/* 初始化 TIM3 编码器和 TIM4 搅拌电机 PWM，默认保持电机停止。 */
uint8_t App_MotorInit(void);

/* 停止 TIM4_CH3 PWM 并清零比较值。 */
void App_MotorStop(void);

/* 创建电机闭环控制任务。 */
uint8_t App_MotorCreateTask(void);


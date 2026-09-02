#pragma once

#include <stdint.h>

/* 初始化 TIM3 编码器和 TIM4 搅拌电机 PWM，默认保持电机停止。 */
uint8_t App_MotorInit(void);

/* 设置 TIM4_CH3 的 PWM 比较值，范围自动限制在 0 到 ARR。 */
uint8_t App_MotorSetPwm(uint16_t compare);

/* 启动 TIM4_CH3 PWM，当前方向控制端保持单向默认电平。 */
uint8_t App_MotorStartPwm(void);

/* 停止 TIM4_CH3 PWM 并清零比较值。 */
void App_MotorStop(void);

/* 创建电机闭环控制任务。 */
uint8_t App_MotorCreateTask(void);

/* 创建临时编码器计数验证任务；测试模式下不启动电机PWM。 */
uint8_t App_MotorCreateEncoderTestTask(void);

/* 获取从编码器测试任务启动后累计的TIM3计数，反向旋转时可能为负值。 */
int32_t App_MotorGetEncoderTestTotalCount(void);


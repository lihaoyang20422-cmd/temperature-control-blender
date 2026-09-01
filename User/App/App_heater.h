#ifndef APP_HEATER_H
#define APP_HEATER_H

#include <stdint.h>

/* 初始化加热安全输出并复位硬件故障锁存器，默认保持 TIM2 PWM 关闭。 */
uint8_t App_HeaterInit(void);

/* 创建加热 PID、温度采集和故障处理任务。 */
uint8_t App_HeaterCreateTask(void);

/* 由统一 HAL EXTI 回调转发；ISR 内只通知任务，不执行 PWM 或报警操作。 */
void App_HeaterExtiCallback(uint16_t gpioPin);

#endif /* APP_HEATER_H */

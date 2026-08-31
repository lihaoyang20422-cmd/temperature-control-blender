/*
 * bsp_pins.h
 * Human-readable names for CubeMX-configured board pins.
 */
#ifndef BSP_PINS_H
#define BSP_PINS_H

#include "main.h"

/* Human-readable names for board wiring.
 * CubeMX still owns GPIO configuration; handwritten modules use these names.
 */

/* Keys: PC6-PC9, internal pull-up, active low. */
#define KEY1_GPIO_Port          GPIOC
#define KEY1_Pin                GPIO_PIN_6
#define KEY2_GPIO_Port          GPIOC
#define KEY2_Pin                GPIO_PIN_7
#define KEY3_GPIO_Port          GPIOC
#define KEY3_Pin                GPIO_PIN_8
#define KEY4_GPIO_Port          GPIOC
#define KEY4_Pin                GPIO_PIN_9
#define KEY_PRESSED_STATE       GPIO_PIN_RESET

/* OLED: I2C1. */
#define OLED_SCL_GPIO_Port      GPIOB
#define OLED_SCL_Pin            GPIO_PIN_6
#define OLED_SDA_GPIO_Port      GPIOB
#define OLED_SDA_Pin            GPIO_PIN_7

/* LSM6DSM: SPI2, PB12 chip select and PC4/PC5 interrupts. */
#define LSM_CS_GPIO_Port        GPIOB
#define LSM_CS_Pin              GPIO_PIN_12
#define LSM_INT1_GPIO_Port      GPIOC
#define LSM_INT1_Pin            GPIO_PIN_4
#define LSM_INT2_GPIO_Port      GPIOC
#define LSM_INT2_Pin            GPIO_PIN_5
#define LSM_CS_ACTIVE_STATE     GPIO_PIN_RESET
#define LSM_CS_INACTIVE_STATE   GPIO_PIN_SET

/* Buzzer: TIM1_CH1. */
#define BUZZ_GPIO_Port          GPIOA
#define BUZZ_Pin                GPIO_PIN_8

/* Motor encoder and PWM pins. */
#define M1_ENC_A_GPIO_Port      GPIOB
#define M1_ENC_A_Pin            GPIO_PIN_4
#define M1_ENC_B_GPIO_Port      GPIOB
#define M1_ENC_B_Pin            GPIO_PIN_5

#define MOTOR1_GPIO_Port        GPIOB
#define MOTOR1_Pin              GPIO_PIN_8
#define MOTOR2_GPIO_Port        GPIOB
#define MOTOR2_Pin              GPIO_PIN_9
#define MOTOR2_FIXED_STATE      GPIO_PIN_RESET

/* Heater and fan enable. */
#define HEAT_PWM_GPIO_Port      GPIOA
#define HEAT_PWM_Pin            GPIO_PIN_0
#define MOTOR_ON_GPIO_Port      GPIOA
#define MOTOR_ON_Pin            GPIO_PIN_1
#define MOTOR_ON_ACTIVE_STATE   GPIO_PIN_SET
#define MOTOR_ON_INACTIVE_STATE GPIO_PIN_RESET

/* ADC inputs. */
#define BOARD_ADC_GPIO_Port     GPIOB
#define BOARD_ADC_Pin           GPIO_PIN_0
#define LIQUID_ADC_GPIO_Port    GPIOB
#define LIQUID_ADC_Pin          GPIO_PIN_1
#define HEAT_I_ADC_GPIO_Port    GPIOA
#define HEAT_I_ADC_Pin          GPIO_PIN_4
#define SUPPLY_24V_ADC_GPIO_Port GPIOC
#define SUPPLY_24V_ADC_Pin       GPIO_PIN_0

/* External RTC interrupt: DS3231 open-drain, active low. */
#define DS_INT_GPIO_Port        GPIOC
#define DS_INT_Pin              GPIO_PIN_12
#define DS_INT_ACTIVE_STATE     GPIO_PIN_RESET

/* Hardware over-current latch. */
#define FAULT_RST_N_GPIO_Port   GPIOC
#define FAULT_RST_N_Pin         GPIO_PIN_2
#define FAULT_RST_ASSERT_STATE  GPIO_PIN_RESET
#define FAULT_RST_RELEASE_STATE GPIO_PIN_SET
#define HEAT_FAULT_N_GPIO_Port  GPIOC
#define HEAT_FAULT_N_Pin        GPIO_PIN_3
#define HEAT_FAULT_ACTIVE_STATE GPIO_PIN_RESET

#endif /* BSP_PINS_H */

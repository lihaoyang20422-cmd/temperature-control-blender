#include "App_buzzer.h"

#include "FreeRTOS.h"
#include "timers.h"
#include "tim.h"

#define APP_BUZZER_PWM_PERIOD 17999U
#define APP_BUZZER_PWM_PULSE  ((APP_BUZZER_PWM_PERIOD + 1U) / 2U)

static TimerHandle_t s_shortTimer;
static volatile uint8_t s_initialized;
static volatile uint8_t s_continuous;

typedef enum
{
    APP_BUZZER_CMD_SHORT = 0,
    APP_BUZZER_CMD_CONTINUOUS_OFF,
    APP_BUZZER_CMD_CONTINUOUS_ON
} App_BuzzerCommand_t;

static void App_BuzzerStopPwm(void)
{
    (void)HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1);
}

static void App_BuzzerStartPwm(void)
{
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, APP_BUZZER_PWM_PULSE);
    (void)HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1);
}

static void App_BuzzerShortTimerCallback(TimerHandle_t timer)
{
    (void)timer;

    /* A continuous fault alarm owns the buzzer and cannot be stopped here. */
    if (s_continuous == 0U)
    {
        App_BuzzerStopPwm();
    }
}

static void App_BuzzerCommandCallback(void *context, uint32_t command)
{
    (void)context;

    if (s_initialized == 0U)
    {
        return;
    }

    if (command == APP_BUZZER_CMD_SHORT)
    {
        if (s_continuous == 0U)
        {
            App_BuzzerStartPwm();
            if (xTimerStart(s_shortTimer, 0U) != pdPASS)
            {
                App_BuzzerStopPwm();
            }
        }
    }
    else
    {
        s_continuous = (command == APP_BUZZER_CMD_CONTINUOUS_ON) ? 1U : 0U;
        (void)xTimerStop(s_shortTimer, 0U);

        if (s_continuous != 0U)
        {
            App_BuzzerStartPwm();
        }
        else
        {
            App_BuzzerStopPwm();
        }
    }
}

static void App_BuzzerPostCommand(App_BuzzerCommand_t command)
{
    BaseType_t higherPriorityTaskWoken = pdFALSE;

    if (__get_IPSR() != 0U)
    {
        if (xTimerPendFunctionCallFromISR(App_BuzzerCommandCallback,
                                          NULL,
                                          (uint32_t)command,
                                          &higherPriorityTaskWoken) == pdPASS)
        {
            portYIELD_FROM_ISR(higherPriorityTaskWoken);
        }
    }
    else
    {
        (void)xTimerPendFunctionCall(App_BuzzerCommandCallback,
                                     NULL,
                                     (uint32_t)command,
                                     0U);
    }
}

uint8_t App_BuzzerInit(void)
{
    if (s_initialized != 0U)
    {
        return 1U;
    }

    /* TIM1 is initialized by MX_TIM1_Init before App_main is entered. */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, APP_BUZZER_PWM_PULSE);
    App_BuzzerStopPwm();

    s_shortTimer = xTimerCreate("Buzzer",
                                pdMS_TO_TICKS(APP_BUZZER_SHORT_MS),
                                pdFALSE,
                                NULL,
                                App_BuzzerShortTimerCallback);
    if (s_shortTimer == NULL)
    {
        return 0U;
    }

    s_continuous = 0U;
    s_initialized = 1U;
    return 1U;
}

void App_BuzzerBeepShort(void)
{
    if ((s_initialized == 0U) || (s_continuous != 0U))
    {
        return;
    }

    App_BuzzerPostCommand(APP_BUZZER_CMD_SHORT);
}

void App_BuzzerSetContinuous(uint8_t enable)
{
    if (s_initialized == 0U)
    {
        return;
    }

    App_BuzzerPostCommand((enable != 0U) ? APP_BUZZER_CMD_CONTINUOUS_ON
                                         : APP_BUZZER_CMD_CONTINUOUS_OFF);
}



#include "Dri_key.h"
#include "bsp_pins.h"

#define DRI_KEY_SCAN_PERIOD_MS   10U
#define DRI_KEY_DEBOUNCE_MS      30U
#define DRI_KEY_DEBOUNCE_SAMPLES (DRI_KEY_DEBOUNCE_MS / DRI_KEY_SCAN_PERIOD_MS)
#define DRI_KEY_LONG_PRESS_MS    600U
#define DRI_KEY_REPEAT_MS        120U

static const uint16_t s_keyPins[DRI_KEY_COUNT] =
{
    KEY1_Pin,
    KEY2_Pin,
    KEY3_Pin,
    KEY4_Pin
};

static GPIO_PinState s_lastRawState[DRI_KEY_COUNT];
static GPIO_PinState s_stableState[DRI_KEY_COUNT];
static uint8_t s_debounceSamples[DRI_KEY_COUNT];
static uint32_t s_holdTimeMs[DRI_KEY_COUNT];
static uint32_t s_repeatTimeMs[DRI_KEY_COUNT];
static uint8_t s_longReported[DRI_KEY_COUNT];
static uint8_t s_keyInitialized;

uint8_t Dri_KeyInit(void)
{
    uint8_t index;

    for (index = 0U; index < DRI_KEY_COUNT; index++)
    {
        s_lastRawState[index] = HAL_GPIO_ReadPin(KEY1_GPIO_Port, s_keyPins[index]);
        s_stableState[index] = s_lastRawState[index];
        s_debounceSamples[index] = 0U;
        s_holdTimeMs[index] = 0U;
        s_repeatTimeMs[index] = 0U;
        s_longReported[index] = 0U;
    }

    s_keyInitialized = 1U;
    return 1U;
}

uint8_t Dri_KeyScan(DriKeyEvent_t *event)
{
    uint8_t index;
    GPIO_PinState rawState;
    uint8_t eventPending = 0U;

    if ((s_keyInitialized == 0U) || (event == NULL))
    {
        return 0U;
    }

    event->key = DRI_KEY_1;
    event->type = DRI_KEY_EVENT_NONE;

    for (index = 0U; index < DRI_KEY_COUNT; index++)
    {
        rawState = HAL_GPIO_ReadPin(KEY1_GPIO_Port, s_keyPins[index]);

        /* 原始电平发生变化时重新计数，连续两次相同才认定为稳定状态。 */
        if (rawState != s_lastRawState[index])
        {
            s_lastRawState[index] = rawState;
            s_debounceSamples[index] = 1U;
            continue;
        }

        if (s_debounceSamples[index] < DRI_KEY_DEBOUNCE_SAMPLES)
        {
            s_debounceSamples[index]++;
            continue;
        }

        if (rawState != s_stableState[index])
        {
            s_stableState[index] = rawState;

            if (rawState == KEY_PRESSED_STATE)
            {
                s_holdTimeMs[index] = 0U;
                s_repeatTimeMs[index] = 0U;
                s_longReported[index] = 0U;
            }
            else
            {
                /* 未达到长按阈值即释放，产生一次短按事件。 */
                if (s_longReported[index] == 0U)
                {
                    if (eventPending == 0U)
                    {
                        event->key = (DriKeyId_t)index;
                        event->type = DRI_KEY_EVENT_SHORT;
                        eventPending = 1U;
                    }
                }

                s_holdTimeMs[index] = 0U;
                s_repeatTimeMs[index] = 0U;
                s_longReported[index] = 0U;
            }
        }

        if (s_stableState[index] == KEY_PRESSED_STATE)
        {
            s_holdTimeMs[index] += DRI_KEY_SCAN_PERIOD_MS;

            if ((s_longReported[index] == 0U) &&
                (s_holdTimeMs[index] >= DRI_KEY_LONG_PRESS_MS))
            {
                s_longReported[index] = 1U;
                s_repeatTimeMs[index] = 0U;
                if (eventPending == 0U)
                {
                    event->key = (DriKeyId_t)index;
                    event->type = DRI_KEY_EVENT_LONG;
                    eventPending = 1U;
                }
            }
            else if (s_longReported[index] != 0U)
            {
                s_repeatTimeMs[index] += DRI_KEY_SCAN_PERIOD_MS;
                if (s_repeatTimeMs[index] >= DRI_KEY_REPEAT_MS)
                {
                    s_repeatTimeMs[index] = 0U;
                    if (eventPending == 0U)
                    {
                        event->key = (DriKeyId_t)index;
                        event->type = DRI_KEY_EVENT_REPEAT;
                        eventPending = 1U;
                    }
                }
            }
        }
    }

    return eventPending;
}

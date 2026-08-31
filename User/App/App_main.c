#include "App_main.h"
#include "Com_debug.h"
#include "App_buzzer.h"
#include "bsp_pins.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_PRINT_TASK_STACK_SIZE          256U
#define APP_BUZZER_TEST_TASK_STACK_SIZE    128U
#define APP_BUZZER_TEST_INTERVAL_MS        5000U
#define APP_BUZZER_TEST_LONG_MS            1000U

typedef enum
{
    APP_TASK_PRIORITY_PRINT = tskIDLE_PRIORITY + 1U,
    APP_TASK_PRIORITY_BUZZER_TEST = tskIDLE_PRIORITY + 1U
} AppTaskPriority_t;

static void App_PrintTask(void *argument);
static void App_BuzzerTestTask(void *argument);

void App_main(void)
{
    if (App_BuzzerInit() == 0U)
    {
        debug_printfln("Buzzer init failed");
        for (;;)
        {
        }
    }

    if (xTaskCreate(App_PrintTask, "Print", APP_PRINT_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_PRINT, NULL) != pdPASS)
    {
        debug_printfln("FreeRTOS print task create failed");
        for (;;)
        {
        }
    }

    if (xTaskCreate(App_BuzzerTestTask, "BuzzerTest", APP_BUZZER_TEST_TASK_STACK_SIZE, NULL, APP_TASK_PRIORITY_BUZZER_TEST, NULL) != pdPASS)
    {
        debug_printfln("FreeRTOS buzzer test task create failed");
        for (;;)
        {
        }
    }

    vTaskStartScheduler();

    for (;;)
    {
    }
}

static void App_PrintTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        debug_printfln("Hello Word \xE6\xB8\xA9\xE6\x8E\xA7\xE6\x90\x85\xE6\x8B\x8C\xE6\x9C\xBA");
        vTaskDelay(pdMS_TO_TICKS(1000U));
    }
}

static void App_BuzzerTestTask(void *argument)
{
    (void)argument;

    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_INTERVAL_MS));
        debug_printfln("Buzzer short beep");
        App_BuzzerBeepShort();

        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_INTERVAL_MS));
        debug_printfln("Buzzer long beep start");
        App_BuzzerSetContinuous(1U);
        vTaskDelay(pdMS_TO_TICKS(APP_BUZZER_TEST_LONG_MS));
        App_BuzzerSetContinuous(0U);
        debug_printfln("Buzzer long beep stop");
    }
}

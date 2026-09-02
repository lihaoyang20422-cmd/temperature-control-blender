#include "App_rtc.h"
#include "App_system.h"
#include "App_ui.h"
#include "Dri_ds3231.h"
#include "Com_debug.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_RTC_UPDATE_PERIOD_MS 1000U
#define APP_RTC_TASK_STACK_SIZE  192U

static uint8_t s_rtcReady;

static void App_RtcPublishTime(const DriDs3231Time_t *time)
{
    if ((time == NULL) || (App_SystemLock(portMAX_DELAY) == 0U))
    {
        return;
    }

    /* 只在互斥锁内复制状态，I2C 读写均在锁外完成。 */
    g_appData.RtcTime.Year = time->year;
    g_appData.RtcTime.Month = time->month;
    g_appData.RtcTime.Date = time->date;
    g_appData.RtcTime.Day = time->day;
    g_appData.RtcTime.Hours = time->hours;
    g_appData.RtcTime.Minutes = time->minutes;
    g_appData.RtcTime.Seconds = time->seconds;
    App_SystemUnlock();
    App_UiNotify();
}

static void App_RtcTask(void *argument)
{
    DriDs3231Time_t time;

    (void)argument;
    for (;;)
    {
        if ((s_rtcReady != 0U) && (Dri_Ds3231ReadTime(&time) != 0U))
        {
            App_RtcPublishTime(&time);
        }
        else
        {
            debug_printfln("DS3231 read failed");
        }
        vTaskDelay(pdMS_TO_TICKS(APP_RTC_UPDATE_PERIOD_MS));
    }
}

uint8_t App_RtcInit(void)
{
    DriDs3231Time_t time;

    if ((Dri_Ds3231Init() == 0U) || (Dri_Ds3231ReadTime(&time) == 0U))
    {
        debug_printfln("DS3231 init failed");
        return 0U;
    }

    s_rtcReady = 1U;
    App_RtcPublishTime(&time);

    debug_printfln("DS3231 ready %04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)time.year, (unsigned int)time.month,
                   (unsigned int)time.date, (unsigned int)time.hours,
                   (unsigned int)time.minutes, (unsigned int)time.seconds);
    return 1U;
}

uint8_t App_RtcCreateTask(void)
{
    return (xTaskCreate(App_RtcTask, "RTC", APP_RTC_TASK_STACK_SIZE, NULL,
                        tskIDLE_PRIORITY + 1U, NULL) == pdPASS) ? 1U : 0U;
}

uint8_t App_RtcSetTime(const DriDs3231Time_t *time)
{
    if ((s_rtcReady == 0U) || (time == NULL) ||
        (Dri_Ds3231WriteTime(time) == 0U))
    {
        return 0U;
    }

    App_RtcPublishTime(time);
    debug_printfln("DS3231 time set %04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)time->year, (unsigned int)time->month,
                   (unsigned int)time->date, (unsigned int)time->hours,
                   (unsigned int)time->minutes, (unsigned int)time->seconds);
    return 1U;
}

#include "App_rtc.h"
#include "Dri_ds3231.h"
#include "Com_debug.h"

uint8_t App_RtcInit(void)
{
    DriDs3231Time_t time;

    if ((Dri_Ds3231Init() == 0U) || (Dri_Ds3231ReadTime(&time) == 0U))
    {
        debug_printfln("DS3231 init failed");
        return 0U;
    }

    debug_printfln("DS3231 ready %04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned int)time.year, (unsigned int)time.month,
                   (unsigned int)time.date, (unsigned int)time.hours,
                   (unsigned int)time.minutes, (unsigned int)time.seconds);
    return 1U;
}

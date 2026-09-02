#include "App_bluetooth.h"
#include "App_rtc.h"
#include "Int_Bluetooth.h"
#include "Com_debug.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define APP_BLUETOOTH_TASK_STACK_SIZE 192U
#define APP_BLUETOOTH_POLL_PERIOD_MS  10U
#define APP_BLUETOOTH_LINE_SIZE       32U
/* 蓝牙名称已经完成一次性配置，正常启动时不再发送 AT 改名指令。 */
/* #define APP_BLUETOOTH_NAME_COMMAND "AT+NAME=Ayang_motor\r\n" */

static uint8_t s_bluetoothReady;

static uint8_t App_BluetoothIsDigit(uint8_t value)
{
    return ((value >= (uint8_t)'0') && (value <= (uint8_t)'9')) ? 1U : 0U;
}

static uint8_t App_BluetoothParseTwoDigits(const char *text, uint8_t *value)
{
    if ((text == NULL) || (value == NULL) ||
        (App_BluetoothIsDigit((uint8_t)text[0]) == 0U) ||
        (App_BluetoothIsDigit((uint8_t)text[1]) == 0U))
    {
        return 0U;
    }

    *value = (uint8_t)(((text[0] - '0') * 10) + (text[1] - '0'));
    return 1U;
}

static uint8_t App_BluetoothWeekday(uint16_t year, uint8_t month, uint8_t date)
{
    static const uint8_t monthTable[12] = { 0U, 3U, 2U, 5U, 0U, 3U,
                                            5U, 1U, 4U, 6U, 2U, 4U };
    uint16_t adjustedYear = year;
    uint8_t weekday;

    if (month < 3U)
    {
        adjustedYear--;
    }
    weekday = (uint8_t)((adjustedYear + adjustedYear / 4U -
                         adjustedYear / 100U + adjustedYear / 400U +
                         monthTable[month - 1U] + date) % 7U);
    return (weekday == 0U) ? 7U : weekday;
}

static uint8_t App_BluetoothParseRtc(const char *line, DriDs3231Time_t *time)
{
    uint8_t month;
    uint8_t date;
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
    uint8_t year;

    /* 为适配 ECB01C 默认 MTU=23，命令采用固定短格式：RTC,YYMMDD,HHMMSS。 */
    if ((line == NULL) || (time == NULL) || (strlen(line) != 17U) ||
        (strncmp(line, "RTC,", 4U) != 0) || (line[10] != ','))
    {
        return 0U;
    }

    if ((App_BluetoothParseTwoDigits(&line[4], &year) == 0U) ||
        (App_BluetoothParseTwoDigits(&line[6], &month) == 0U) ||
        (App_BluetoothParseTwoDigits(&line[8], &date) == 0U) ||
        (App_BluetoothParseTwoDigits(&line[11], &hours) == 0U) ||
        (App_BluetoothParseTwoDigits(&line[13], &minutes) == 0U) ||
        (App_BluetoothParseTwoDigits(&line[15], &seconds) == 0U))
    {
        return 0U;
    }

    time->year = (uint16_t)(2000U + year);
    time->month = month;
    time->date = date;
    time->day = (month >= 1U) && (month <= 12U) ?
                App_BluetoothWeekday(time->year, month, date) : 0U;
    time->hours = hours;
    time->minutes = minutes;
    time->seconds = seconds;
    return 1U;
}

static void App_BluetoothHandleLine(const char *line)
{
    DriDs3231Time_t time;
    static const uint8_t okText[] = "RTC_OK\r\n";
    static const uint8_t errorText[] = "RTC_ERROR\r\n";

    /* ECB01C 会回显 AT 指令并返回 OK/ERROR，连接和断开时也会主动上报状态。 */
    if ((strncmp(line, "AT", 2U) == 0) ||
        (strcmp(line, "OK") == 0) ||
        (strcmp(line, "ERROR") == 0) ||
        (line[0] == '+') ||
        (strcmp(line, "CONNECT OK") == 0) ||
        (strcmp(line, "DISCONNECT") == 0))
    {
        debug_printfln("ECB01C: %s", line);
        return;
    }

    if (App_BluetoothParseRtc(line, &time) != 0U)
    {
        if (App_RtcSetTime(&time) != 0U)
        {
            (void)Int_BluetoothSend(okText, (uint16_t)(sizeof(okText) - 1U));
            return;
        }
    }

    (void)Int_BluetoothSend(errorText, (uint16_t)(sizeof(errorText) - 1U));
}

static void App_BluetoothTask(void *argument)
{
    uint8_t data;
    uint8_t line[APP_BLUETOOTH_LINE_SIZE];
    uint16_t length = 0U;

    (void)argument;
    for (;;)
    {
        while (Int_BluetoothReadByte(&data) != 0U)
        {
            if (data == (uint8_t)'\n')
            {
                line[length] = '\0';
                if (length > 0U)
                {
                    App_BluetoothHandleLine((const char *)line);
                }
                length = 0U;
            }
            else if (data != (uint8_t)'\r')
            {
                if (length < (APP_BLUETOOTH_LINE_SIZE - 1U))
                {
                    line[length++] = data;
                }
                else
                {
                    length = 0U;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(APP_BLUETOOTH_POLL_PERIOD_MS));
    }
}

uint8_t App_BluetoothInit(void)
{
    s_bluetoothReady = Int_BluetoothInit();
    if (s_bluetoothReady == 0U)
    {
        debug_printfln("Bluetooth USART2 init failed");
    }
    /*
     * 蓝牙名称已经配置为 Ayang_motor，ECB01C 会在芯片内部保存该参数。
     * 如需再次改名，可临时恢复下面的 AT 指令发送代码，烧录运行一次后重新注释。
     */
    /*
    if (Int_BluetoothSend((const uint8_t *)APP_BLUETOOTH_NAME_COMMAND,
                          (uint16_t)(sizeof(APP_BLUETOOTH_NAME_COMMAND) - 1U)) == 0U)
    {
        debug_printfln("ECB01C name command send failed");
    }
    */
    return s_bluetoothReady;
}

uint8_t App_BluetoothCreateTask(void)
{
    if (s_bluetoothReady == 0U)
    {
        return 0U;
    }

    return (xTaskCreate(App_BluetoothTask, "Bluetooth",
                        APP_BLUETOOTH_TASK_STACK_SIZE, NULL,
                        tskIDLE_PRIORITY + 1U, NULL) == pdPASS) ? 1U : 0U;
}

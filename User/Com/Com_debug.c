#include "Com_debug.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>

#define COM_DEBUG_BUFFER_SIZE 256U
#define COM_VOFA_BUFFER_SIZE   64U

void Com_DebugPrintf(const char *format, ...)
{
#ifdef COM_DEBUG_ENABLE
    char buffer[COM_DEBUG_BUFFER_SIZE];
    va_list args;
    int length;

    if (format == NULL)
    {
        return;
    }

    va_start(args, format);
    length = vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    if (length <= 0)
    {
        return;
    }

    if ((uint32_t)length >= sizeof(buffer))
    {
        length = (int)(sizeof(buffer) - 1U);
    }

    (void)HAL_UART_Transmit(&huart1,
                            (uint8_t *)buffer,
                            (uint16_t)length,
                            200U);
#else
    (void)format;
#endif
}

void Com_VofaSendMotorFrame(int16_t targetRpm, int16_t currentRpm,
                            uint16_t dutyPercent, int32_t encoderDelta)
{
#ifdef COM_VOFA_ENABLE
    char buffer[COM_VOFA_BUFFER_SIZE];
    int length;

    /*
     * FireWater 使用逗号分隔的 ASCII 数值，并以换行结束一帧。
     * USART1 在 VOFA 模式下禁止混入普通日志，否则 VOFA 无法稳定识别通道。
     */
    length = snprintf(buffer, sizeof(buffer), "%d,%d,%u,%ld\r\n",
                      (int)targetRpm, (int)currentRpm,
                      (unsigned int)dutyPercent, (long)encoderDelta);
    if (length <= 0)
    {
        return;
    }

    if ((uint32_t)length >= sizeof(buffer))
    {
        length = (int)(sizeof(buffer) - 1U);
    }

    (void)HAL_UART_Transmit(&huart1,
                            (uint8_t *)buffer,
                            (uint16_t)length,
                            200U);
#else
    (void)targetRpm;
    (void)currentRpm;
    (void)dutyPercent;
    (void)encoderDelta;
#endif
}

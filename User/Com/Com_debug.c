#include "Com_debug.h"
#include "usart.h"

#include <stdarg.h>
#include <stdio.h>

#define COM_DEBUG_BUFFER_SIZE 256U

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

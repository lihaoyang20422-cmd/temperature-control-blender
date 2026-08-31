#ifndef __COM_DEBUG_H__
#define __COM_DEBUG_H__

#include <stdint.h>

void Com_DebugPrintf(const char *format, ...);

#define COM_DEBUG_ENABLE

#ifdef  COM_DEBUG_ENABLE

#define debug_printf(format, ...) Com_DebugPrintf("[%s:%d]" format, __FILE__, __LINE__, ##__VA_ARGS__)
#define debug_printfln(format, ...) Com_DebugPrintf("[%s:%d]" format "\r\n", __FILE__, __LINE__, ##__VA_ARGS__)

#else
#define debug_printf(format, ...)
#define debug_printfln(format, ...)
#endif // DEBUG
#endif /* __COM_DEBUG_H__ */

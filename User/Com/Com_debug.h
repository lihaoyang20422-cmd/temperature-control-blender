#ifndef __COM_DEBUG_H__
#define __COM_DEBUG_H__

#include <stdint.h>

/* 普通调试日志与 VOFA 波形数据不能共用 USART1，否则会破坏 FireWater 数据帧。 */
#if defined(COM_DEBUG_ENABLE) && defined(COM_VOFA_ENABLE)
#error "COM_DEBUG_ENABLE and COM_VOFA_ENABLE cannot be enabled together"
#endif

void Com_DebugPrintf(const char *format, ...);

/* 按 FireWater 文本协议发送一帧电机波形数据：目标转速、当前转速、占空比、编码器增量。 */
void Com_VofaSendMotorFrame(int16_t targetRpm, int16_t currentRpm,
                            uint16_t dutyPercent, int32_t encoderDelta);

#ifdef  COM_DEBUG_ENABLE

#define debug_printf(format, ...) Com_DebugPrintf("[%s:%d]" format, __FILE__, __LINE__, ##__VA_ARGS__)
#define debug_printfln(format, ...) Com_DebugPrintf("[%s:%d]" format "\r\n", __FILE__, __LINE__, ##__VA_ARGS__)

#else
#define debug_printf(format, ...)
#define debug_printfln(format, ...)
#endif /* COM_DEBUG_ENABLE */
#endif /* __COM_DEBUG_H__ */

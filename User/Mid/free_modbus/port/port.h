#ifndef FREEMODBUS_PORT_H
#define FREEMODBUS_PORT_H

#include <stdint.h>
#ifndef NDEBUG
/* ARMCC工程未链接断言运行库，发布固件关闭FreeModbus内部调试断言。 */
#define NDEBUG
#endif
#include <assert.h>
#include "FreeRTOS.h"
#include "task.h"

#define INLINE                      inline
#define PR_BEGIN_EXTERN_C           extern "C" {
#define PR_END_EXTERN_C             }

typedef uint8_t                     BOOL;
typedef uint8_t                     UCHAR;
typedef int8_t                      CHAR;
typedef uint16_t                    USHORT;
typedef int16_t                     SHORT;
typedef uint32_t                    ULONG;
typedef int32_t                     LONG;

#ifndef TRUE
#define TRUE                        1U
#endif

#ifndef FALSE
#define FALSE                       0U
#endif

#define ENTER_CRITICAL_SECTION()    taskENTER_CRITICAL()
#define EXIT_CRITICAL_SECTION()     taskEXIT_CRITICAL()

#endif /* FREEMODBUS_PORT_H */

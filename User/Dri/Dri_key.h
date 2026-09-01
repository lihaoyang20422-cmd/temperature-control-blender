#ifndef DRI_KEY_H
#define DRI_KEY_H

#include <stdint.h>

/* 四个面板按键的逻辑编号，编号与原理图 KEY1~KEY4 一致。 */
typedef enum
{
    DRI_KEY_1 = 0U,
    DRI_KEY_2,
    DRI_KEY_3,
    DRI_KEY_4,
    DRI_KEY_COUNT
} DriKeyId_t;

/* 按键事件类型：短按在释放时产生，长按和重复事件在按住期间产生。 */
typedef enum
{
    DRI_KEY_EVENT_NONE = 0U,
    DRI_KEY_EVENT_SHORT,
    DRI_KEY_EVENT_LONG,
    DRI_KEY_EVENT_REPEAT
} DriKeyEventType_t;

/* 一次扫描返回的按键事件。 */
typedef struct
{
    DriKeyId_t key;
    DriKeyEventType_t type;
} DriKeyEvent_t;

/* 初始化按键扫描状态，GPIO 输入模式由 CubeMX 生成代码完成。 */
uint8_t Dri_KeyInit(void);

/* 扫描一次按键，检测到事件时返回 1；调用周期固定为 10 ms。 */
uint8_t Dri_KeyScan(DriKeyEvent_t *event);

#endif /* DRI_KEY_H */

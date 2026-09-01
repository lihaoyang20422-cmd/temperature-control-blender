#ifndef APP_KEY_H
#define APP_KEY_H

#include <stdint.h>
#include "Dri_key.h"

/* 处理底层按键事件，修改焦点、目标参数和电机运行状态。 */
void App_KeyHandleEvent(const DriKeyEvent_t *event);

#endif /* APP_KEY_H */

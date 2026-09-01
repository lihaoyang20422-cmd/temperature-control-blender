#ifndef APP_UI_H
#define APP_UI_H

#include "FreeRTOS.h"
#include "task.h"

/* UI 任务：获取状态快照并在解锁后刷新 OLED。 */
void App_UiTask(void *argument);

/* 通知 UI 任务立即刷新，通知会在任务忙时合并。 */
void App_UiNotify(void);

#endif /* APP_UI_H */

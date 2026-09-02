#ifndef APP_UI_H
#define APP_UI_H

#include "FreeRTOS.h"
#include "task.h"

typedef enum
{
    APP_UI_PAGE_HOME = 0,
    APP_UI_PAGE_SETTINGS
} AppUiPage_t;

/* UI 任务：获取状态快照并在解锁后刷新 OLED。 */
void App_UiTask(void *argument);

/* 通知 UI 任务立即刷新，通知会在任务忙时合并。 */
void App_UiNotify(void);

/* 查询当前页面，按键层据此避免首页按键误修改参数或启动设备。 */
AppUiPage_t App_UiGetPage(void);

/* 从开机首页进入设置页面，返回 1 表示本次发生了页面切换。 */
uint8_t App_UiEnterSettingsPage(void);

#endif /* APP_UI_H */

#ifndef APP_OLED_H
#define APP_OLED_H

#include <stdint.h>
#include "App_system.h"

/* 初始化 SSD1315，并显示启动画面。 */
uint8_t App_OledInit(void);

/* 根据共享状态快照刷新 OLED 五行界面，调用者不得持有系统状态互斥锁。 */
uint8_t App_OledUpdate(const AppData_t *data, const AppFocusState_t *focus);

#endif /* APP_OLED_H */

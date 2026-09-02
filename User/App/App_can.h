#ifndef APP_CAN_H
#define APP_CAN_H

#include <stdint.h>

/* 初始化CAN过滤器、启动CAN控制器并开启FIFO0接收通知。 */
uint8_t App_CanInit(void);

/* 创建CAN接收处理任务。 */
uint8_t App_CanCreateTask(void);

#endif /* APP_CAN_H */

#ifndef APP_PROTOCOL_H
#define APP_PROTOCOL_H

#include <stdint.h>

/* 蓝牙、CAN 和 Modbus 共用的寄存器编号，协议地址由各自适配层转换。 */
#define APP_REG_TARGET_TEMP          0x0000U
#define APP_REG_TARGET_SPEED         0x0001U
#define APP_REG_TARGET_TIME_SEC      0x0002U
#define APP_REG_TARGET_TIME_MIN      0x0003U
#define APP_REG_RUN_CONTROL          0x0004U
#define APP_REG_SYSTEM_STATUS        0x0005U
#define APP_REG_CURRENT_TEMP         0x0006U
#define APP_REG_CURRENT_SPEED        0x0007U
#define APP_REG_REMAINING_TIME       0x0008U
#define APP_REG_UID0_HIGH            0x0009U
#define APP_REG_UID0_LOW             0x000AU
#define APP_REG_UID1_HIGH            0x000BU
#define APP_REG_UID1_LOW             0x000CU
#define APP_REG_UID2_HIGH            0x000DU
#define APP_REG_UID2_LOW             0x000EU
#define APP_REG_ELAPSED_TIME         0x000FU
#define APP_REG_CURRENT_BOARD_TEMP   0x0010U
#define APP_REG_FAULT_FLAGS          0x0011U
#define APP_REG_MAX                  APP_REG_FAULT_FLAGS

typedef enum
{
    APP_PROTOCOL_OK = 0,
    APP_PROTOCOL_BAD_REG,
    APP_PROTOCOL_BAD_VALUE
} AppProtocolStatus_t;

AppProtocolStatus_t App_ProtocolReadRegister(uint16_t reg, uint16_t *value);
AppProtocolStatus_t App_ProtocolValidateWrite(uint16_t reg, uint16_t value);
AppProtocolStatus_t App_ProtocolWriteRegister(uint16_t reg, uint16_t value);
AppProtocolStatus_t App_ProtocolWriteRegisterDeferred(uint16_t reg, uint16_t value,
                                                      uint8_t *needsCommit);
uint8_t App_ProtocolCommitDeferred(uint8_t needsCommit);

#endif /* APP_PROTOCOL_H */

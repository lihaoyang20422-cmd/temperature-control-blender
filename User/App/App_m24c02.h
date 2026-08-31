#ifndef APP_M24C02_H
#define APP_M24C02_H

#include <stdint.h>

/* EEPROM 自检的详细结果，便于串口区分通信、校验和恢复故障。 */
typedef enum
{
    APP_M24C02_TEST_OK = 0,
    APP_M24C02_TEST_DEVICE_NOT_READY,
    APP_M24C02_TEST_BACKUP_READ_FAILED,
    APP_M24C02_TEST_WRITE_FAILED,
    APP_M24C02_TEST_READ_FAILED,
    APP_M24C02_TEST_DATA_MISMATCH,
    APP_M24C02_TEST_RESTORE_WRITE_FAILED,
    APP_M24C02_TEST_RESTORE_READ_FAILED,
    APP_M24C02_TEST_RESTORE_MISMATCH
} AppM24C02TestResult_t;

/*
 * 对 EEPROM 最后一页执行一次无损读写自检。
 * 函数会先备份原数据，测试完成后恢复并校验原数据，禁止在中断中调用。
 */
AppM24C02TestResult_t App_M24C02SelfTest(void);

#endif /* APP_M24C02_H */

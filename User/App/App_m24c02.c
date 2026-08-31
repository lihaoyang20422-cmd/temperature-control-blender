#include "App_m24c02.h"
#include "drv_m24c02.h"

#define APP_M24C02_TEST_ADDRESS  (M24C02_SIZE_BYTES - M24C02_PAGE_SIZE_BYTES)
#define APP_M24C02_TEST_SIZE     5U

/* 逐字节比较，避免应用自检额外依赖 C 标准库。 */
static uint8_t App_M24C02DataEqual(const uint8_t *left,
                                   const uint8_t *right,
                                   uint16_t size)
{
    uint16_t index;

    for (index = 0U; index < size; index++)
    {
        if (left[index] != right[index])
        {
            return 0U;
        }
    }

    return 1U;
}

AppM24C02TestResult_t App_M24C02SelfTest(void)
{
    uint8_t backupData[APP_M24C02_TEST_SIZE];
    uint8_t testData[APP_M24C02_TEST_SIZE];
    uint8_t readData[APP_M24C02_TEST_SIZE];
    uint16_t index;
    AppM24C02TestResult_t testResult;
    static const uint8_t testPattern[APP_M24C02_TEST_SIZE] =
    {
        0x11U, 0x22U, 0x33U, 0x44U, 0x55U
    };

    if (M24C02_IsReady() == 0U)
    {
        return APP_M24C02_TEST_DEVICE_NOT_READY;
    }

    /* 只有成功备份测试页后才允许写入，避免无法恢复原始数据。 */
    if (M24C02_Read((uint8_t)APP_M24C02_TEST_ADDRESS,
                    backupData,
                    APP_M24C02_TEST_SIZE) == 0U)
    {
        return APP_M24C02_TEST_BACKUP_READ_FAILED;
    }

    /* 使用指定的固定测试序列，验证写入和读回数据是否完全一致。 */
    for (index = 0U; index < APP_M24C02_TEST_SIZE; index++)
    {
        testData[index] = testPattern[index];
    }

    testResult = APP_M24C02_TEST_OK;
    if (M24C02_Write((uint8_t)APP_M24C02_TEST_ADDRESS,
                     testData,
                     APP_M24C02_TEST_SIZE) == 0U)
    {
        testResult = APP_M24C02_TEST_WRITE_FAILED;
    }
    else if (M24C02_Read((uint8_t)APP_M24C02_TEST_ADDRESS,
                         readData,
                         APP_M24C02_TEST_SIZE) == 0U)
    {
        testResult = APP_M24C02_TEST_READ_FAILED;
    }
    else if (App_M24C02DataEqual(testData, readData, APP_M24C02_TEST_SIZE) == 0U)
    {
        testResult = APP_M24C02_TEST_DATA_MISMATCH;
    }

    /*
     * 写测试数据后无论测试是否通过都必须恢复原页。
     * 恢复失败优先返回，因为它表示测试区域的原数据可能已经受损。
     */
    if (M24C02_Write((uint8_t)APP_M24C02_TEST_ADDRESS,
                     backupData,
                     APP_M24C02_TEST_SIZE) == 0U)
    {
        return APP_M24C02_TEST_RESTORE_WRITE_FAILED;
    }

    if (M24C02_Read((uint8_t)APP_M24C02_TEST_ADDRESS,
                    readData,
                    APP_M24C02_TEST_SIZE) == 0U)
    {
        return APP_M24C02_TEST_RESTORE_READ_FAILED;
    }

    if (App_M24C02DataEqual(backupData, readData, APP_M24C02_TEST_SIZE) == 0U)
    {
        return APP_M24C02_TEST_RESTORE_MISMATCH;
    }

    return testResult;
}

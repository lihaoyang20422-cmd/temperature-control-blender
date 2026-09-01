#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "App_oled.h"
#include "Com_debug.h"
#include "Int_I2C1.h"
#include "driver_ssd1315.h"
#include "FreeRTOS.h"
#include "task.h"

#define APP_OLED_I2C_TIMEOUT_MS  100U
#define APP_OLED_TEXT_X          44U
#define APP_OLED_TEXT_Y          24U
#define APP_OLED_TEXT_LENGTH     5U
#define APP_OLED_LINE_X          0U
#define APP_OLED_LINE_FONT       SSD1315_FONT_12

static ssd1315_handle_t s_oledHandle;
static ssd1315_address_t s_oledAddress = SSD1315_ADDR_SA0_0;

static uint8_t App_OledI2cInit(void);
static uint8_t App_OledI2cDeinit(void);
static uint8_t App_OledI2cWrite(uint8_t addr, uint8_t control, uint8_t *data, uint16_t length);
static uint8_t App_OledDummyInit(void);
static uint8_t App_OledDummyDeinit(void);
static uint8_t App_OledDummyWrite(uint8_t value);
static uint8_t App_OledDummySpiWrite(uint8_t *data, uint16_t length);
static void App_OledDelayMs(uint32_t milliseconds);
static void App_OledDebugPrint(const char *const format, ...);
static const char *App_OledStatusName(AppMotorStatusValue_t status);
static char App_OledFocusMark(const AppFocusState_t *focus, AppFocusItem_t item);

static uint8_t App_OledCheck(uint8_t result, const char *name)
{
    if (result != 0U)
    {
        Com_DebugPrintf("SSD1315 %s failed: %u\r\n", name, (unsigned int)result);
        return 0U;
    }

    return 1U;
}

static uint8_t App_OledI2cInit(void)
{
    /* 总线互斥锁已由 App_main 创建，重复调用保持幂等。 */
    return (Bsp_I2c1Init() != 0U) ? 0U : 1U;
}

static uint8_t App_OledI2cDeinit(void)
{
    return 0U;
}

static uint8_t App_OledI2cWrite(uint8_t addr, uint8_t control, uint8_t *data, uint16_t length)
{
    /* SSD1315 一次整页最多传输 128 字节，首字节为命令/数据控制字节。 */
    static uint8_t transmitBuffer[129];

    if ((data == NULL) || (length > 128U))
    {
        return 1U;
    }

    transmitBuffer[0] = control;
    (void)memcpy(&transmitBuffer[1], data, length);
    return (Bsp_I2c1MasterTransmit(addr, transmitBuffer, (uint16_t)(length + 1U),
                                   APP_OLED_I2C_TIMEOUT_MS) == HAL_OK) ? 0U : 1U;
}

static uint8_t App_OledDummyInit(void)
{
    return 0U;
}

static uint8_t App_OledDummyDeinit(void)
{
    return 0U;
}

static uint8_t App_OledDummyWrite(uint8_t value)
{
    (void)value;
    return 0U;
}

static uint8_t App_OledDummySpiWrite(uint8_t *data, uint16_t length)
{
    (void)data;
    (void)length;
    return 0U;
}

static void App_OledDelayMs(uint32_t milliseconds)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        vTaskDelay(pdMS_TO_TICKS(milliseconds));
    }
    else
    {
        /* App_main 在调度器启动前调用 OLED 初始化，此时 HAL 时基可直接使用。 */
        HAL_Delay(milliseconds);
    }
}

static void App_OledDebugPrint(const char *const format, ...)
{
    char buffer[160];
    va_list arguments;

    va_start(arguments, format);
    (void)vsnprintf(buffer, sizeof(buffer), format, arguments);
    va_end(arguments);
    Com_DebugPrintf("%s", buffer);
}

uint8_t App_OledInit(void)
{
    char text[] = "Ayang";

    if (Bsp_I2c1IsDeviceReady(SSD1315_ADDR_SA0_0, 3U, 20U) == HAL_OK)
    {
        s_oledAddress = SSD1315_ADDR_SA0_0;
    }
    else if (Bsp_I2c1IsDeviceReady(SSD1315_ADDR_SA0_1, 3U, 20U) == HAL_OK)
    {
        s_oledAddress = SSD1315_ADDR_SA0_1;
    }
    else
    {
        Com_DebugPrintf("SSD1315 not found on I2C1\r\n");
        return 0U;
    }

    DRIVER_SSD1315_LINK_INIT(&s_oledHandle, ssd1315_handle_t);
    DRIVER_SSD1315_LINK_IIC_INIT(&s_oledHandle, App_OledI2cInit);
    DRIVER_SSD1315_LINK_IIC_DEINIT(&s_oledHandle, App_OledI2cDeinit);
    DRIVER_SSD1315_LINK_IIC_WRITE(&s_oledHandle, App_OledI2cWrite);
    DRIVER_SSD1315_LINK_SPI_INIT(&s_oledHandle, App_OledDummyInit);
    DRIVER_SSD1315_LINK_SPI_DEINIT(&s_oledHandle, App_OledDummyDeinit);
    DRIVER_SSD1315_LINK_SPI_WRITE_COMMAND(&s_oledHandle, App_OledDummySpiWrite);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_INIT(&s_oledHandle, App_OledDummyInit);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_DEINIT(&s_oledHandle, App_OledDummyDeinit);
    DRIVER_SSD1315_LINK_SPI_COMMAND_DATA_GPIO_WRITE(&s_oledHandle, App_OledDummyWrite);
    DRIVER_SSD1315_LINK_RESET_GPIO_INIT(&s_oledHandle, App_OledDummyInit);
    DRIVER_SSD1315_LINK_RESET_GPIO_DEINIT(&s_oledHandle, App_OledDummyDeinit);
    DRIVER_SSD1315_LINK_RESET_GPIO_WRITE(&s_oledHandle, App_OledDummyWrite);
    DRIVER_SSD1315_LINK_DELAY_MS(&s_oledHandle, App_OledDelayMs);
    DRIVER_SSD1315_LINK_DEBUG_PRINT(&s_oledHandle, App_OledDebugPrint);

    (void)ssd1315_set_interface(&s_oledHandle, SSD1315_INTERFACE_IIC);
    (void)ssd1315_set_addr_pin(&s_oledHandle, s_oledAddress);

    if (App_OledCheck(ssd1315_init(&s_oledHandle), "init") == 0U)
    {
        return 0U;
    }

    /* 设置 SSD1315 常用参数，打开内部电荷泵并启用显示。 */
    if ((App_OledCheck(ssd1315_set_display(&s_oledHandle, SSD1315_DISPLAY_OFF), "display off") == 0U) ||
        (App_OledCheck(ssd1315_set_display_clock(&s_oledHandle, 0x08U, 0x00U), "clock") == 0U) ||
        (App_OledCheck(ssd1315_set_multiplex_ratio(&s_oledHandle, 0x3FU), "multiplex") == 0U) ||
        (App_OledCheck(ssd1315_set_display_offset(&s_oledHandle, 0x00U), "offset") == 0U) ||
        (App_OledCheck(ssd1315_set_display_start_line(&s_oledHandle, 0x00U), "start line") == 0U) ||
        (App_OledCheck(ssd1315_set_charge_pump(&s_oledHandle, SSD1315_CHARGE_PUMP_ENABLE,
                                               SSD1315_CHARGE_PUMP_MODE_8P5V), "charge pump") == 0U) ||
        (App_OledCheck(ssd1315_set_segment_remap(&s_oledHandle, SSD1315_SEGMENT_COLUMN_ADDRESS_127), "segment") == 0U) ||
        (App_OledCheck(ssd1315_set_scan_direction(&s_oledHandle, SSD1315_SCAN_DIRECTION_COMN_1_START), "scan") == 0U) ||
        (App_OledCheck(ssd1315_set_com_pins_hardware_conf(&s_oledHandle, SSD1315_PIN_CONF_ALTERNATIVE,
                                                          SSD1315_LEFT_RIGHT_REMAP_DISABLE), "com pins") == 0U) ||
        (App_OledCheck(ssd1315_set_contrast(&s_oledHandle, 0xFFU), "contrast") == 0U) ||
        (App_OledCheck(ssd1315_set_precharge_period(&s_oledHandle, 0x01U, 0x0FU), "precharge") == 0U) ||
        (App_OledCheck(ssd1315_set_deselect_level(&s_oledHandle, SSD1315_DESELECT_LEVEL_0P83), "deselect") == 0U) ||
        (App_OledCheck(ssd1315_set_entire_display(&s_oledHandle, SSD1315_ENTIRE_DISPLAY_OFF), "entire display") == 0U) ||
        (App_OledCheck(ssd1315_set_memory_addressing_mode(&s_oledHandle,
                                                          SSD1315_MEMORY_ADDRESSING_MODE_PAGE), "address mode") == 0U) ||
        (App_OledCheck(ssd1315_set_display_mode(&s_oledHandle, SSD1315_DISPLAY_MODE_NORMAL), "display mode") == 0U) ||
        (App_OledCheck(ssd1315_set_display(&s_oledHandle, SSD1315_DISPLAY_ON), "display on") == 0U))
    {
        return 0U;
    }

    if ((App_OledCheck(ssd1315_clear(&s_oledHandle), "clear") == 0U) ||
        (ssd1315_gram_write_string(&s_oledHandle, APP_OLED_TEXT_X, APP_OLED_TEXT_Y,
                                   text, APP_OLED_TEXT_LENGTH, 1U, SSD1315_FONT_16) != 0U) ||
        (App_OledCheck(ssd1315_gram_update(&s_oledHandle), "update") == 0U))
    {
        return 0U;
    }

    Com_DebugPrintf("SSD1315 ready, display Ayang at center\r\n");
    return 1U;
}

static const char *App_OledStatusName(AppMotorStatusValue_t status)
{
    switch (status)
    {
        case APP_MOTOR_STATUS_IDLE:
            return "IDLE";

        case APP_MOTOR_STATUS_RUNNING:
            return "RUNNING";

        case APP_MOTOR_STATUS_FAULT:
            return "FAULT";

        default:
            return "UNKNOWN";
    }
}

static char App_OledFocusMark(const AppFocusState_t *focus, AppFocusItem_t item)
{
    if ((focus != NULL) && (focus->Current == item))
    {
        return '>';
    }

    return ' ';
}

uint8_t App_OledUpdate(const AppData_t *data, const AppFocusState_t *focus)
{
    char line[24];
    int length;

    if ((s_oledHandle.inited != 1U) || (data == NULL) || (focus == NULL))
    {
        return 0U;
    }

    /* 仅清空 RAM 显存，完整组帧后一次性刷新，避免先清屏造成可见闪烁。 */
    (void)memset(s_oledHandle.gram, 0, sizeof(s_oledHandle.gram));

    /* UID 的完整 96 位在 12 像素字体下无法放入一行，这里显示 UID 的 64 位高信息。 */
    (void)snprintf(line, sizeof(line), "ID:%08lX%08lX",
                   (unsigned long)data->Uid[1], (unsigned long)data->Uid[2]);
    length = (int)strlen(line);
    (void)ssd1315_gram_write_string(&s_oledHandle, APP_OLED_LINE_X, 0U,
                                    line, (uint16_t)length, 1U, APP_OLED_LINE_FONT);

    (void)snprintf(line, sizeof(line), "%cTEMP:%d/%dC",
                   App_OledFocusMark(focus, APP_FOCUS_TEMPERATURE),
                   (int)data->CurrentTemperature, (int)data->TargetTemperature);
    length = (int)strlen(line);
    (void)ssd1315_gram_write_string(&s_oledHandle, APP_OLED_LINE_X, 12U,
                                    line, (uint16_t)length, 1U, APP_OLED_LINE_FONT);

    (void)snprintf(line, sizeof(line), "%cSPEED:%d/%d",
                   App_OledFocusMark(focus, APP_FOCUS_SPEED),
                   (int)data->CurrentSpeed, (int)data->TargetSpeed);
    length = (int)strlen(line);
    (void)ssd1315_gram_write_string(&s_oledHandle, APP_OLED_LINE_X, 24U,
                                    line, (uint16_t)length, 1U, APP_OLED_LINE_FONT);

    (void)snprintf(line, sizeof(line), "%cTIME:%lu/%lus",
                   App_OledFocusMark(focus, APP_FOCUS_TIME),
                   (unsigned long)data->CurrentTime, (unsigned long)data->TargetTime);
    length = (int)strlen(line);
    (void)ssd1315_gram_write_string(&s_oledHandle, APP_OLED_LINE_X, 36U,
                                    line, (uint16_t)length, 1U, APP_OLED_LINE_FONT);

    (void)snprintf(line, sizeof(line), " STATUS:%s", App_OledStatusName(data->CurrentStatus));
    length = (int)strlen(line);
    (void)ssd1315_gram_write_string(&s_oledHandle, APP_OLED_LINE_X, 48U,
                                    line, (uint16_t)length, 1U, APP_OLED_LINE_FONT);

    return (ssd1315_gram_update(&s_oledHandle) == 0U) ? 1U : 0U;
}

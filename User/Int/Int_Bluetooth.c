#include "Int_Bluetooth.h"
#include "usart.h"
#include "stm32f1xx_hal.h"

#define INT_BLUETOOTH_RX_BUFFER_SIZE 128U

static uint8_t s_rxByte;
static uint8_t s_rxBuffer[INT_BLUETOOTH_RX_BUFFER_SIZE];
static volatile uint16_t s_rxHead;
static volatile uint16_t s_rxTail;
static uint8_t s_initialized;

uint8_t Int_BluetoothInit(void)
{
    s_rxHead = 0U;
    s_rxTail = 0U;
    s_initialized = 0U;

    if (HAL_UART_Receive_IT(&huart2, &s_rxByte, 1U) != HAL_OK)
    {
        return 0U;
    }

    s_initialized = 1U;
    return 1U;
}

uint8_t Int_BluetoothReadByte(uint8_t *data)
{
    uint16_t tail;

    if ((data == NULL) || (s_initialized == 0U) || (s_rxTail == s_rxHead))
    {
        return 0U;
    }

    tail = s_rxTail;
    *data = s_rxBuffer[tail];
    s_rxTail = (uint16_t)((tail + 1U) % INT_BLUETOOTH_RX_BUFFER_SIZE);
    return 1U;
}

uint8_t Int_BluetoothSend(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) || (s_initialized == 0U))
    {
        return 0U;
    }

    return (HAL_UART_Transmit(&huart2, (uint8_t *)data, length, 200U) == HAL_OK) ? 1U : 0U;
}

/* HAL 串口回调运行在 USART2 中断上下文，只入队字节，不执行解析、I2C 或 RTC 操作。 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    uint16_t nextHead;

    if ((huart == &huart2) && (s_initialized != 0U))
    {
        nextHead = (uint16_t)((s_rxHead + 1U) % INT_BLUETOOTH_RX_BUFFER_SIZE);
        if (nextHead != s_rxTail)
        {
            s_rxBuffer[s_rxHead] = s_rxByte;
            s_rxHead = nextHead;
        }

        (void)HAL_UART_Receive_IT(&huart2, &s_rxByte, 1U);
    }
}

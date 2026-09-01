#include "Dri_adc.h"
#include "adc.h"

#define DRI_ADC_CHANNEL_COUNT    4U

static uint16_t s_adcDmaBuffer[DRI_ADC_CHANNEL_COUNT];
static uint8_t s_adcStarted;

uint8_t Dri_AdcReadAll(DriAdcSample_t *sample)
{
    if (sample == NULL)
    {
        return 0U;
    }

    /* 首次使用前校准 ADC，然后启动 DMA 循环接收。 */
    if (s_adcStarted == 0U)
    {
        if (HAL_ADCEx_Calibration_Start(&hadc1) != HAL_OK)
        {
            return 0U;
        }
        if (HAL_ADC_Start_DMA(&hadc1, (uint32_t *)s_adcDmaBuffer,
                             DRI_ADC_CHANNEL_COUNT) != HAL_OK)
        {
            return 0U;
        }
        s_adcStarted = 1U;
        /* DMA 刚启动时缓冲区尚未形成完整四路序列，下次调用再返回快照。 */
        return 0U;
    }

    sample->heatCurrent = s_adcDmaBuffer[0];
    sample->boardNtc = s_adcDmaBuffer[1];
    sample->liquidNtc = s_adcDmaBuffer[2];
    sample->supply24V = s_adcDmaBuffer[3];
    return 1U;
}

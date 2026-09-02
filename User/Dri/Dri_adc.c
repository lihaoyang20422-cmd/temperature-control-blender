#include "Dri_adc.h"
#include "adc.h"

#define DRI_ADC_CHANNEL_COUNT    4U
#define DRI_ADC_FILTER_SAMPLES   8U

/* DMA 连续保存 8 组完整扫描序列，用于软件平均滤波。*/
static volatile uint16_t s_adcDmaBuffer[DRI_ADC_CHANNEL_COUNT * DRI_ADC_FILTER_SAMPLES];
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
                             DRI_ADC_CHANNEL_COUNT * DRI_ADC_FILTER_SAMPLES) != HAL_OK)
        {
            return 0U;
        }
        s_adcStarted = 1U;
        /* DMA 刚启动时缓冲区尚未形成完整四路序列，下次调用再返回快照。 */
        return 0U;
    }

    {
        uint32_t sums[DRI_ADC_CHANNEL_COUNT] = {0U, 0U, 0U, 0U};
        uint8_t sequence;
        uint8_t channel;

        /* 每次读取对最近 8 组扫描结果求平均，降低 ADC 噪声和通道串扰影响。*/
        for (sequence = 0U; sequence < DRI_ADC_FILTER_SAMPLES; sequence++)
        {
            for (channel = 0U; channel < DRI_ADC_CHANNEL_COUNT; channel++)
            {
                sums[channel] += s_adcDmaBuffer[(sequence * DRI_ADC_CHANNEL_COUNT) + channel];
            }
        }

        sample->heatCurrent = (uint16_t)(sums[0] / DRI_ADC_FILTER_SAMPLES);
        sample->boardNtc = (uint16_t)(sums[1] / DRI_ADC_FILTER_SAMPLES);
        sample->liquidNtc = (uint16_t)(sums[2] / DRI_ADC_FILTER_SAMPLES);
        sample->supply24V = (uint16_t)(sums[3] / DRI_ADC_FILTER_SAMPLES);
    }
    return 1U;
}

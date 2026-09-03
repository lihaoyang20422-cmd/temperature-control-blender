#include "Dri_adc.h"
#include "adc.h"

#define DRI_ADC_CHANNEL_COUNT    4U
#define DRI_ADC_FILTER_SAMPLES   8U
#define DRI_ADC_HALF_SIZE        (DRI_ADC_CHANNEL_COUNT * DRI_ADC_FILTER_SAMPLES)
#define DRI_ADC_BUFFER_SIZE      (DRI_ADC_HALF_SIZE * 2U)
#define DRI_ADC_SNAPSHOT_RETRIES 3U

/* DMA 双半缓冲区各保存 8 组完整扫描序列，供任务读取非活动半区。 */
static volatile uint16_t s_adcDmaBuffer[DRI_ADC_BUFFER_SIZE];
static uint8_t s_adcStarted;

uint8_t Dri_AdcReadAll(DriAdcSample_t *sample)
{
    uint16_t snapshot[DRI_ADC_HALF_SIZE];
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
                             DRI_ADC_BUFFER_SIZE) != HAL_OK)
        {
            return 0U;
        }
        s_adcStarted = 1U;
        /* DMA 刚启动时缓冲区尚未形成完整四路序列，下次调用再返回快照。 */
        return 0U;
    }

    {
        uint32_t sums[DRI_ADC_CHANNEL_COUNT];
        uint8_t retry;
        uint16_t dmaRemainingBefore;
        uint16_t dmaRemainingAfter;
        uint16_t sourceOffset;
        uint16_t index;
        uint8_t sequence;
        uint8_t channel;
        uint8_t stable = 0U;

        /*
         * CNDTR 大于半区长度时 DMA 正在写前半区，反之正在写后半区。
         * 任务只复制当前非活动半区，并在复制前后检查 DMA 是否跨过半区边界。
         */
        for (retry = 0U; retry < DRI_ADC_SNAPSHOT_RETRIES; retry++)
        {
            dmaRemainingBefore = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
            sourceOffset = (dmaRemainingBefore > DRI_ADC_HALF_SIZE) ?
                           DRI_ADC_HALF_SIZE : 0U;
            for (index = 0U; index < DRI_ADC_HALF_SIZE; index++)
            {
                snapshot[index] = s_adcDmaBuffer[sourceOffset + index];
            }
            dmaRemainingAfter = __HAL_DMA_GET_COUNTER(hadc1.DMA_Handle);
            if (((dmaRemainingBefore > DRI_ADC_HALF_SIZE) &&
                 (dmaRemainingAfter > DRI_ADC_HALF_SIZE)) ||
                ((dmaRemainingBefore <= DRI_ADC_HALF_SIZE) &&
                 (dmaRemainingAfter <= DRI_ADC_HALF_SIZE) &&
                 (dmaRemainingAfter <= dmaRemainingBefore)))
            {
                stable = 1U;
                break;
            }
        }
        if (stable == 0U)
        {
            return 0U;
        }

        for (channel = 0U; channel < DRI_ADC_CHANNEL_COUNT; channel++)
        {
            sums[channel] = 0U;
        }
        for (sequence = 0U; sequence < DRI_ADC_FILTER_SAMPLES; sequence++)
        {
            for (channel = 0U; channel < DRI_ADC_CHANNEL_COUNT; channel++)
            {
                sums[channel] += snapshot[(sequence * DRI_ADC_CHANNEL_COUNT) + channel];
            }
        }

        sample->heatCurrent = (uint16_t)(sums[0] / DRI_ADC_FILTER_SAMPLES);
        sample->boardNtc = (uint16_t)(sums[1] / DRI_ADC_FILTER_SAMPLES);
        sample->liquidNtc = (uint16_t)(sums[2] / DRI_ADC_FILTER_SAMPLES);
        sample->supply24V = (uint16_t)(sums[3] / DRI_ADC_FILTER_SAMPLES);
    }
    return 1U;
}

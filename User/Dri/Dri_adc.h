#ifndef DRI_ADC_H
#define DRI_ADC_H

#include <stdint.h>

/* ADC1 扫描结果，顺序与 CubeMX 中 Rank1~Rank4 一致。 */
typedef struct
{
    uint16_t heatCurrent;  /* PA4，ADC1_IN4。 */
    uint16_t boardNtc;     /* PB0，ADC1_IN8。 */
    uint16_t liquidNtc;    /* PB1，ADC1_IN9。 */
    uint16_t supply24V;    /* PC0，ADC1_IN10。 */
} DriAdcSample_t;

/* 读取 ADC1 的四路扫描结果，成功返回 1。 */
uint8_t Dri_AdcReadAll(DriAdcSample_t *sample);

#endif /* DRI_ADC_H */

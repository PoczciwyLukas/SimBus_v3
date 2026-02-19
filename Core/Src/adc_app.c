#include "adc_app.h"

#include "adc.h"
#include "app.h"
#include "stm32g0xx_hal.h"

#include "board_conf.h"

#include <string.h>

#define ADC_CH_COUNT                BOARD_ADC_CH_COUNT

/* strojenie (w board_conf.h) */
#define ADC_REPORT_THRESHOLD_RAW    BOARD_ADC_REPORT_THRESHOLD_RAW
#define ADC_IIR_SHIFT               BOARD_ADC_IIR_SHIFT

static uint16_t s_adc_dma[ADC_CH_COUNT];
static uint16_t s_adc_filt[ADC_CH_COUNT];
static uint16_t s_adc_last_sent[ADC_CH_COUNT];
static uint8_t  s_inited = 0;

void ADCAPP_Init(void)
{
    if (s_inited) return;

    memset(s_adc_dma, 0, sizeof(s_adc_dma));
    memset(s_adc_filt, 0, sizeof(s_adc_filt));
    memset(s_adc_last_sent, 0, sizeof(s_adc_last_sent));

    (void)HAL_ADCEx_Calibration_Start(&hadc1);

    if (HAL_ADC_Start_DMA(&hadc1, (uint32_t*)s_adc_dma, ADC_CH_COUNT) != HAL_OK) {
        Error_Handler();
    }

    for (uint32_t i = 0; i < ADC_CH_COUNT; i++) {
        s_adc_filt[i] = s_adc_dma[i] & 0x0FFFu;
        s_adc_last_sent[i] = s_adc_filt[i];
    }

    s_inited = 1;
}

static inline uint16_t u16_abs_diff(uint16_t a, uint16_t b)
{
    return (a > b) ? (a - b) : (b - a);
}

void ADCAPP_Process(void)
{
    if (!s_inited) return;

    for (uint16_t ch = 0; ch < ADC_CH_COUNT; ch++)
    {
        uint16_t x = (uint16_t)(s_adc_dma[ch] & 0x0FFFu);
        uint16_t y = s_adc_filt[ch];

        int32_t diff = (int32_t)x - (int32_t)y;
        y = (uint16_t)((int32_t)y + (diff >> ADC_IIR_SHIFT));
        s_adc_filt[ch] = y;

        if (u16_abs_diff(y, s_adc_last_sent[ch]) > ADC_REPORT_THRESHOLD_RAW) {
            s_adc_last_sent[ch] = y;
            (void)App_PostEvent(APP_EVT_ANALOG_VALUE, ch, (int16_t)y, 0u);
        }
    }
}

void ADCAPP_EmitSnapshot(void)
{
    if (!s_inited) return;

    for (uint16_t ch = 0; ch < ADC_CH_COUNT; ch++) {
        uint16_t y = s_adc_filt[ch];
        s_adc_last_sent[ch] = y;
        (void)App_PostEvent(APP_EVT_ANALOG_VALUE, ch, (int16_t)y, 0u);
    }
}

#ifndef ADC_APP_H
#define ADC_APP_H

#include <stdint.h>
#include <stdbool.h>

void ADCAPP_Init(void);
void ADCAPP_Process(void);

/* Snapshot (na STATE_REQUEST) */
void ADCAPP_EmitSnapshot(void);

#endif /* ADC_APP_H */

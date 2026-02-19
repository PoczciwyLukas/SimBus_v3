#ifndef I2C_RESILIENT_H
#define I2C_RESILIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "stm32g0xx_hal.h"

/* Proste, produkcyjne API z retry + recovery. */
HAL_StatusTypeDef I2C1_MemWrite8_Retry(uint16_t devAddr8, uint16_t reg, const uint8_t *data, uint16_t len);
HAL_StatusTypeDef I2C1_MemRead8_Retry (uint16_t devAddr8, uint16_t reg,       uint8_t *data, uint16_t len);

/* Recovery magistrali I2C1 (PB8=SCL, PB9=SDA). */
void I2C1_BusRecover(void);

#ifdef __cplusplus
}
#endif

#endif /* I2C_RESILIENT_H */

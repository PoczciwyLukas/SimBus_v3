#ifndef ENCODER_H
#define ENCODER_H

#include <stdint.h>

void Encoder_Init(void);

/* Call from HAL_GPIO_EXTI_Callback(GPIO_Pin) */
void Encoder_OnExti(uint16_t gpio_pin);

#endif /* ENCODER_H */

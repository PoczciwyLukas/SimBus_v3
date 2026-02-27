#pragma once

#include <stdint.h>
#include <stdbool.h>

void SBusCAN_Init(void);
void SBusCAN_Service(void);

uint8_t SBusCAN_GetModuleId(void);
uint8_t SBusCAN_GetEffectiveModuleId(void);

bool SBusCAN_SendIdentity(void);
bool SBusCAN_SendHeartbeat(void);
bool SBusCAN_SendEncoderStep(uint8_t enc_index_0_5, int16_t delta);
bool SBusCAN_SendDigitalChange(uint8_t index_0_31, uint8_t value_0_1);
bool SBusCAN_SendAnalogValue(uint8_t index_0_5, uint16_t raw_0_4095);

void SBusCAN_SetBacklight(uint8_t bl_index_0_1, uint16_t value_0_1000);

void SBusCAN_HandleRx(uint16_t can_id_11bit, const uint8_t *data, uint8_t len);
void SBusCAN_OnErrorFromISR(uint32_t errorStatusIts);

uint8_t SBusCAN_GetAutodetect(void);
void SBusCAN_SetAutodetect(uint8_t en, uint32_t duration_ms);

uint32_t SBusCAN_GetBusOffCount(void);
uint32_t SBusCAN_GetRecoverCount(void);
uint32_t SBusCAN_GetLastErrorFlags(void);

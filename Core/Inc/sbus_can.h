#pragma once

#include <stdint.h>
#include <stdbool.h>

/* Init warstwy CAN (logika protokołu). */
void SBusCAN_Init(void);

/* module_id (1..63) assigned to this slave. If not assigned yet, returns 0. */
uint8_t SBusCAN_GetModuleId(void);

/* Effective module_id used in CAN IDs. If module_id==0, returns a UID-derived temporary ID (1..63) to avoid collisions. */
uint8_t SBusCAN_GetEffectiveModuleId(void);

/* ===================== TX (telegramy) ===================== */
bool SBusCAN_SendIdentity(void);
bool SBusCAN_SendHeartbeat(void);

bool SBusCAN_SendEncoderStep(uint8_t enc_index_0_5, int16_t delta);
bool SBusCAN_SendDigitalChange(uint8_t index_0_31, uint8_t value_0_1);
bool SBusCAN_SendAnalogValue(uint8_t index_0_5, uint16_t raw_0_4095);

/* Backlight – ustaw PWM lokalnie (0..1000). */
void SBusCAN_SetBacklight(uint8_t bl_index_0_1, uint16_t value_0_1000);

/* ===================== RX (odbiór) ===================== */
void SBusCAN_HandleRx(uint16_t can_id_11bit, const uint8_t data[8]);

/* ===================== Serwis okresowy ===================== */
/* bus-off recovery. Wołaj w pętli głównej (np. co kilka ms). */
void SBusCAN_Service(void);

/* Diagnostyka */
uint32_t SBusCAN_GetBusOffCount(void);
uint32_t SBusCAN_GetRecoverCount(void);
uint32_t SBusCAN_GetLastErrorFlags(void);

/* Hook z ISR (HAL_FDCAN_ErrorStatusCallback w fdcan.c) */
void SBusCAN_OnErrorFromISR(uint32_t errorStatusIts);

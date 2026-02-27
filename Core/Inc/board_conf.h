#pragma once

#include <stdint.h>

/* Project-level defaults (module id, counts, protocol constants) */
#include "sbus_conf.h"

/* =========================
 *  Board-level configuration
 *  (single place for tunables)
 *
 *  IMPORTANT:
 *  These are *defaults* so the firmware builds and runs without extra
 *  per-panel configuration. Later we can move selected parameters into
 *  "panel profiles" (loaded from flash / sent by PC) if you want.
 * ========================= */

/* ===================== Scheduling / periodic tasks ===================== */

/* Heartbeat period (telemetry) */
#ifndef BOARD_PERIOD_HEARTBEAT_MS
#define BOARD_PERIOD_HEARTBEAT_MS           1000u
#endif

/* MCP23017 scan period (I2C polling of digital inputs) */
#ifndef BOARD_PERIOD_MCP_POLL_MS
#define BOARD_PERIOD_MCP_POLL_MS            5u
#endif

/* ADC processing/reporting period */
#ifndef BOARD_PERIOD_ADC_PROC_MS
#define BOARD_PERIOD_ADC_PROC_MS            10u
#endif

/* CAN service pump period (drain RX queue / push TX) */
#ifndef BOARD_PERIOD_CAN_SERVICE_MS
#define BOARD_PERIOD_CAN_SERVICE_MS         1u
#endif

/* ===================== CAN recovery timing (after BUSOFF) ===================== */

#ifndef BOARD_CAN_BUSOFF_RECOVER_DELAY_MS
#define BOARD_CAN_BUSOFF_RECOVER_DELAY_MS   200u
#endif

/* Backward-compat alias (older code used this name) */
#ifndef BOARD_CAN_RECOVER_DELAY_MS
#define BOARD_CAN_RECOVER_DELAY_MS          BOARD_CAN_BUSOFF_RECOVER_DELAY_MS
#endif

/* ===================== ADC processing ===================== */

/* ADC channel count sampled via DMA.
 * For RevA we align this 1:1 to SBUS analog inputs.
 */
#ifndef BOARD_ADC_CH_COUNT
#define BOARD_ADC_CH_COUNT                  SBUS_ANA_COUNT
#endif

/* IIR filter strength: y += (x - y) >> shift */
#ifndef BOARD_ADC_IIR_SHIFT
#define BOARD_ADC_IIR_SHIFT                 4u
#endif

/* Report only when delta exceeds threshold (raw ADC units) */
#ifndef BOARD_ADC_REPORT_THRESHOLD_RAW
#define BOARD_ADC_REPORT_THRESHOLD_RAW      8u
#endif

/* ===================== Backlight PWM ===================== */

/* PWM scale (0..TOP). Must match TIM3 ARR from CubeMX (tim.c). */
#ifndef BOARD_BL_PWM_MAX
#define BOARD_BL_PWM_MAX                    1000u
#endif

/* Backward/forward alias: some code uses BOARD_BACKLIGHT_PWM_MAX */
#ifndef BOARD_BACKLIGHT_PWM_MAX
#define BOARD_BACKLIGHT_PWM_MAX             BOARD_BL_PWM_MAX
#endif

/* ===================== Optional backlight potentiometer ===================== */
/*
 * Map one ADC channel to backlight duty (local pot).
 * - Disable by leaving BOARD_BACKLIGHT_POT_ADC_INDEX as 0xFF.
 * - If enabled: ADC raw (0..4095) is mapped to 0..100% PWM.
 */
#ifndef BOARD_BACKLIGHT_POT_ADC_INDEX
#define BOARD_BACKLIGHT_POT_ADC_INDEX       (0xFFu) /* 0..(SBUS_ANA_COUNT-1) or 0xFF to disable */
#endif

#ifndef BOARD_BACKLIGHT_POT_BL_INDEX
#define BOARD_BACKLIGHT_POT_BL_INDEX        (0u)    /* 0..(SBUS_BL_COUNT-1) */
#endif

/* ===================== Persistent storage ===================== */
/*
 * Flash address used to store module_id (NVM record).
 * Default points to the last Flash page for STM32G0B1C (128KB Flash):
 * 0x08000000 + 128KB - FLASH_PAGE_SIZE (typically 2KB) = 0x0801F800.
 * If your linker script changes FLASH length, adjust accordingly.
 */
#ifndef BOARD_NVM_FLASH_ADDR
#define BOARD_NVM_FLASH_ADDR                (0x0801F800u)
#endif


#ifndef BOARD_NVM_MAGIC
#define BOARD_NVM_MAGIC                     (0xA5u)
#endif

#ifndef BOARD_BACKLIGHT_DEFAULT_U8
#define BOARD_BACKLIGHT_DEFAULT_U8          (64u)
#endif

#ifndef BOARD_AUTODETECT_DEFAULT_MS
#define BOARD_AUTODETECT_DEFAULT_MS         (10000u)
#endif

#ifndef BOARD_CAN_STATUS_PERIOD_MS
#define BOARD_CAN_STATUS_PERIOD_MS          (1000u)
#endif

#ifndef BOARD_CAN_ERROR_PERIOD_MS
#define BOARD_CAN_ERROR_PERIOD_MS           (250u)
#endif

#ifndef BOARD_WATCHDOG_TIMEOUT_MS
#define BOARD_WATCHDOG_TIMEOUT_MS           (1000u)
#endif

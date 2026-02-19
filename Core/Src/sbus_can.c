#include "sbus_can.h"
#include "sbus_conf.h"
#include "board_conf.h"

#include "fdcan.h"
#include "tim.h"

#include "stm32g0xx_hal.h"
#include "stm32g0xx.h" /* UID_BASE */

/* =======================
 *  Internal helpers
 * ======================= */

static uint8_t s_module_id = SBUS_MODULE_ID_DEFAULT;
static uint32_t s_uid_xor = 0u;

/* autodetect */
static uint8_t  s_autodetect = 0u;
static uint32_t s_autodetect_until_ms = 0u;

/* bus-off recovery */
static uint32_t s_last_tx_err = 0u;
static uint32_t s_last_rx_err = 0u;
static uint32_t s_busoff_count = 0u;
static uint32_t s_last_busoff_ms = 0u;

/* CAN tx gating */
static uint32_t s_next_tx_allowed_ms = 0u;

/* PWM backlight cache */
static uint8_t s_pwm_last = 255u; /* invalid => force first update */

/* =======================
 *  Utility
 * ======================= */

static uint16_t u16_abs_diff(uint16_t a, uint16_t b) {
    return (a >= b) ? (uint16_t)(a - b) : (uint16_t)(b - a);
}

static uint32_t app_now_ms(void) {
    return HAL_GetTick();
}

static void pwm_set_u8(uint8_t duty_u8) {
    /* duty_u8: 0..255 -> CCR scaled to ARR */
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
    uint32_t ccr = ((uint32_t)duty_u8 * (arr + 1u)) / 255u;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
    s_pwm_last = duty_u8;
}

/* =======================
 *  Minimal NVM (module id)
 * ======================= */

static bool sbus_nvm_load_module_id(uint8_t *module_id_out) {
    if (module_id_out == NULL) return false;

#if defined(BOARD_NVM_FLASH_ADDR)
    const uint32_t addr = (uint32_t)BOARD_NVM_FLASH_ADDR;
    const uint8_t *p = (const uint8_t *)addr;

    /* very small: [0]=magic, [1]=module_id, [2]=~module_id, [3]=~magic */
    const uint8_t magic = p[0];
    const uint8_t mid   = p[1];
    const uint8_t mid_n = p[2];
    const uint8_t mag_n = p[3];

    if ((uint8_t)~magic != mag_n) return false;
    if ((uint8_t)~mid   != mid_n) return false;
    if (magic != (uint8_t)BOARD_NVM_MAGIC) return false;

    *module_id_out = mid;
    return true;
#else
    (void)module_id_out;
    return false;
#endif
}

static uint8_t sbus_effective_mid(void) {
    /* If module_id already known => use it */
    if (s_module_id != 0u) return s_module_id;

    /* otherwise fall back to UID-derived pseudo-id (non-persistent) */
    uint8_t mid = (uint8_t)((s_uid_xor & 0x3Fu) + 1u); /* 1..64 => then clamp to 1..63 */
    if (mid > 63u) mid = 63u;
    return mid;
}

/* =======================
 *  Public API
 * ======================= */

uint8_t SBusCAN_GetModuleId(void) { return s_module_id; }

void SBusCAN_SetAutodetect(uint8_t en, uint32_t duration_ms) {
    s_autodetect = (en != 0u) ? 1u : 0u;
    if (s_autodetect) {
        s_autodetect_until_ms = app_now_ms() + duration_ms;
    } else {
        s_autodetect_until_ms = 0u;
    }
}

uint8_t SBusCAN_GetAutodetect(void) { return s_autodetect; }

void SBusCAN_Init(void) {
    /* UID xor */
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    s_uid_xor = uid[0] ^ uid[1] ^ uid[2];

    s_last_tx_err = 0u;
    s_last_rx_err = 0u;
    s_busoff_count = 0u;
    s_last_busoff_ms = 0u;
    s_next_tx_allowed_ms = 0u;

    s_autodetect = 0;
    s_autodetect_until_ms = 0u;

    /* Load module ID (persisted), with sanity check */
    s_module_id = SBUS_MODULE_ID_DEFAULT;
    (void)sbus_nvm_load_module_id(&s_module_id);
    if (s_module_id > 63u) {
        s_module_id = SBUS_MODULE_ID_DEFAULT;
    }

    /* Ensure PWM running (TIM3 CH1 configured by CubeMX) */
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    pwm_set_u8((uint8_t)BOARD_BACKLIGHT_DEFAULT_U8);
}

static bool can_tx_allowed_now(uint32_t now_ms) {
    return (now_ms >= s_next_tx_allowed_ms);
}

static void can_backoff_ms(uint32_t now_ms, uint32_t backoff_ms) {
    s_next_tx_allowed_ms = now_ms + backoff_ms;
}

static uint32_t can_calc_backoff_ms(uint32_t tx_err, uint32_t rx_err) {
    /* simple policy: if errors rising => back off a bit */
    uint32_t sum = tx_err + rx_err;
    if (sum < 10u)  return 0u;
    if (sum < 50u)  return 2u;
    if (sum < 100u) return 5u;
    return 10u;
}

/* =======================
 *  Frame encode/decode
 * ======================= */

static void pack_u16_le(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static uint16_t unpack_u16_le(const uint8_t *p) {
    return (uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8);
}

static bool sbus_can_tx(uint32_t id, const uint8_t *data, uint8_t dlc) {
    FDCAN_TxHeaderTypeDef h;
    h.Identifier = id;
    h.IdType = FDCAN_STANDARD_ID;
    h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = (uint32_t)dlc << 16; /* HAL expects DLC encoding; Cube/HAL uses <<16 macro style */
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF;
    h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    h.MessageMarker = 0u;

    if (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, (uint8_t*)data) != HAL_OK) {
        return false;
    }
    return true;
}

/* =======================
 *  Service / RX handler
 * ======================= */

void SBusCAN_Service(void) {
    const uint32_t now = app_now_ms();

    /* autodetect window expiration */
    if (s_autodetect && (s_autodetect_until_ms != 0u) && (now >= s_autodetect_until_ms)) {
        s_autodetect = 0u;
        s_autodetect_until_ms = 0u;
    }

    /* Read error counters */
    uint32_t tx_err = 0u, rx_err = 0u;
    (void)HAL_FDCAN_GetErrorCounters(&hfdcan1, (FDCAN_ErrorCountersTypeDef *)&(FDCAN_ErrorCountersTypeDef){
        .TxErrorCnt = 0, .RxErrorCnt = 0, .RxErrorPassive = 0, .TxErrorPassive = 0
    });

    /* NOTE: Some Cube/HAL versions require a real variable; do it properly: */
    FDCAN_ErrorCountersTypeDef ec;
    if (HAL_FDCAN_GetErrorCounters(&hfdcan1, &ec) == HAL_OK) {
        tx_err = ec.TxErrorCnt;
        rx_err = ec.RxErrorCnt;
    }

    /* apply simple backoff */
    if ((tx_err != s_last_tx_err) || (rx_err != s_last_rx_err)) {
        uint32_t bo = can_calc_backoff_ms(tx_err, rx_err);
        if (bo != 0u) {
            can_backoff_ms(now, bo);
        }
        s_last_tx_err = tx_err;
        s_last_rx_err = rx_err;
    }

    /* TODO: periodic status frames (heartbeat etc.) can live here later */
}

static void on_cmd_set_backlight(const uint8_t *pl, uint8_t len) {
    if (len < 1u) return;
    const uint8_t duty = pl[0];
    if (duty != s_pwm_last) {
        pwm_set_u8(duty);
    }
}

static void on_cmd_set_autodetect(const uint8_t *pl, uint8_t len) {
    if (len < 5u) return;
    const uint8_t en = pl[0];
    const uint32_t dur = (uint32_t)pl[1] | ((uint32_t)pl[2] << 8) | ((uint32_t)pl[3] << 16) | ((uint32_t)pl[4] << 24);
    SBusCAN_SetAutodetect(en, dur);
}

static void on_cmd_ping(const uint8_t *pl, uint8_t len) {
    (void)pl; (void)len;

    const uint32_t now = app_now_ms();
    if (!can_tx_allowed_now(now)) return;

    uint8_t data[8] = {0};
    const uint8_t mid = sbus_effective_mid();

    /* Example PONG payload:
     * [0]=MID
     * [1]=FW_MAJOR
     * [2]=FW_MINOR
     * [3]=flags
     * [4..7]=uid_xor (LE)
     */
    data[0] = mid;
    data[1] = (uint8_t)SBUS_FW_VER_MAJOR;
    data[2] = (uint8_t)SBUS_FW_VER_MINOR;
    data[3] = 0u;
    data[4] = (uint8_t)(s_uid_xor & 0xFFu);
    data[5] = (uint8_t)((s_uid_xor >> 8) & 0xFFu);
    data[6] = (uint8_t)((s_uid_xor >> 16) & 0xFFu);
    data[7] = (uint8_t)((s_uid_xor >> 24) & 0xFFu);

    (void)sbus_can_tx(SBUS_CAN_ID_PONG, data, 8u);
}

void SBusCAN_HandleRx(uint32_t id, const uint8_t *payload, uint8_t len) {
    if (payload == NULL) return;

    /* very early filtering could be done via HW filters; here keep SW decode simple */

    switch (id) {
        case SBUS_CAN_ID_PING:
            on_cmd_ping(payload, len);
            break;

        case SBUS_CAN_ID_SET_BACKLIGHT:
            on_cmd_set_backlight(payload, len);
            break;

        case SBUS_CAN_ID_SET_AUTODETECT:
            on_cmd_set_autodetect(payload, len);
            break;

        case SBUS_CAN_ID_SET_MODULE_ID:
            /* payload: [0]=new_id, [1]=~new_id, [2]=magic, [3]=~magic, [4..] reserved */
            if (len >= 4u) {
                const uint8_t new_id = payload[0];
                const uint8_t new_id_n = payload[1];
                const uint8_t magic = payload[2];
                const uint8_t magic_n = payload[3];

                if (((uint8_t)~new_id == new_id_n) &&
                    ((uint8_t)~magic == magic_n) &&
                    (magic == (uint8_t)BOARD_NVM_MAGIC) &&
                    (new_id <= 63u)) {
                    s_module_id = new_id;
                    /* NOTE: storing to flash will be added later; for now runtime only */
                }
            }
            break;

        default:
            /* Unknown / not handled yet */
            break;
    }
}

/* =======================
 *  HAL callback glue
 * ======================= */

void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs) {
    if (hfdcan == NULL) return;

    if ((RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0u) {
        FDCAN_RxHeaderTypeDef rh;
        uint8_t data[8];

        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &rh, data) == HAL_OK) {
            const uint32_t id = rh.Identifier;
            uint8_t dlc = 0u;

            /* HAL DLC decode: DataLength is encoded in bits[19:16] in some versions.
             * We used (dlc<<16) for TX; here decode similarly. */
            dlc = (uint8_t)((rh.DataLength >> 16) & 0xFu);
            if (dlc > 8u) dlc = 8u;

            SBusCAN_HandleRx(id, data, dlc);
        }
    }
}

void HAL_FDCAN_ErrorCallback(FDCAN_HandleTypeDef *hfdcan) {
    if (hfdcan == NULL) return;

    const uint32_t err = HAL_FDCAN_GetError(hfdcan);
    (void)err;

    /* Bus-off handling: count and remember timestamp */
    if ((err & HAL_FDCAN_ERROR_BUSOFF) != 0u) {
        s_busoff_count++;
        s_last_busoff_ms = app_now_ms();
        /* Recovery policy: Cube/HAL may auto-recover if configured; we just backoff TX a bit */
        can_backoff_ms(s_last_busoff_ms, 50u);
    }
}

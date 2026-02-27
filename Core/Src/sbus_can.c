#include "sbus_can.h"

#include "sbus_conf.h"
#include "board_conf.h"
#include "fdcan.h"
#include "tim.h"

#include "stm32g0xx.h"
#include "stm32g0xx_hal.h"

static uint8_t  s_module_id = SBUS_MODULE_ID_DEFAULT;
static uint32_t s_uid_xor = 0u;

static uint8_t  s_autodetect = 0u;
static uint32_t s_autodetect_until_ms = 0u;

static uint32_t s_last_error_flags = 0u;
static uint32_t s_busoff_count = 0u;
static uint32_t s_recover_count = 0u;
static uint32_t s_next_tx_allowed_ms = 0u;

static uint8_t  s_pwm_last = 0xFFu;
static uint32_t s_last_status_ms = 0u;
static uint32_t s_last_error_ms = 0u;

static uint32_t app_now_ms(void) { return HAL_GetTick(); }

static uint32_t crc32_soft(const uint8_t *data, uint32_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (uint32_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint32_t b = 0; b < 8u; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1u)));
        }
    }
    return ~crc;
}

typedef struct
{
    uint32_t magic;
    uint8_t module_id;
    uint8_t reserved[3];
    uint32_t crc32;
} sbus_nvm_record_t;

static bool sbus_nvm_load_module_id(uint8_t *module_id_out)
{
    if (module_id_out == NULL) return false;

    const sbus_nvm_record_t *rec = (const sbus_nvm_record_t *)BOARD_NVM_FLASH_ADDR;
    if (rec->magic != ((uint32_t)BOARD_NVM_MAGIC | ((uint32_t)BOARD_NVM_MAGIC << 8) |
                       ((uint32_t)BOARD_NVM_MAGIC << 16) | ((uint32_t)BOARD_NVM_MAGIC << 24))) {
        return false;
    }

    const uint32_t calc = crc32_soft((const uint8_t *)rec, sizeof(*rec) - sizeof(uint32_t));
    if (calc != rec->crc32) return false;
    if (rec->module_id > 63u) return false;

    *module_id_out = rec->module_id;
    return true;
}

static bool sbus_nvm_store_module_id(uint8_t module_id)
{
    sbus_nvm_record_t rec;
    rec.magic = ((uint32_t)BOARD_NVM_MAGIC | ((uint32_t)BOARD_NVM_MAGIC << 8) |
                 ((uint32_t)BOARD_NVM_MAGIC << 16) | ((uint32_t)BOARD_NVM_MAGIC << 24));
    rec.module_id = module_id;
    rec.reserved[0] = 0xFFu;
    rec.reserved[1] = 0xFFu;
    rec.reserved[2] = 0xFFu;
    rec.crc32 = crc32_soft((const uint8_t *)&rec, sizeof(rec) - sizeof(uint32_t));

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.Page = (BOARD_NVM_FLASH_ADDR - FLASH_BASE) / FLASH_PAGE_SIZE;
    erase.NbPages = 1u;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK) {
        HAL_FLASH_Lock();
        return false;
    }

    const uint64_t *q = (const uint64_t *)&rec;
    uint32_t addr = BOARD_NVM_FLASH_ADDR;
    for (uint32_t i = 0; i < (sizeof(rec) / sizeof(uint64_t)); i++) {
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, q[i]) != HAL_OK) {
            HAL_FLASH_Lock();
            return false;
        }
        addr += 8u;
    }

    HAL_FLASH_Lock();
    return true;
}

static uint8_t sbus_effective_mid(void)
{
    if (s_module_id != 0u) return s_module_id;
    uint8_t mid = (uint8_t)((s_uid_xor & 0x3Fu) + 1u);
    if (mid > 63u) mid = 63u;
    return mid;
}

static uint32_t sbus_tel_id(uint8_t ppp)
{
    return (uint32_t)SBUS_MAKE_STDID(ppp, SBUS_TT_TEL, sbus_effective_mid());
}

static void pwm_set_u8(uint8_t duty_u8)
{
    uint32_t arr = __HAL_TIM_GET_AUTORELOAD(&htim3);
    uint32_t ccr = ((uint32_t)duty_u8 * (arr + 1u)) / 255u;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, ccr);
    s_pwm_last = duty_u8;
}

static bool can_tx_allowed_now(uint32_t now_ms)
{
    return (now_ms >= s_next_tx_allowed_ms);
}

static bool sbus_can_tx(uint32_t id, const uint8_t *data, uint8_t dlc)
{
    if (dlc > 8u) return false;

    const uint32_t now = app_now_ms();
    if (!can_tx_allowed_now(now)) return false;

    FDCAN_TxHeaderTypeDef h = {0};
    h.Identifier = (id & 0x7FFu);
    h.IdType = FDCAN_STANDARD_ID;
    h.TxFrameType = FDCAN_DATA_FRAME;
    h.DataLength = ((uint32_t)dlc << 16);
    h.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    h.BitRateSwitch = FDCAN_BRS_OFF;
    h.FDFormat = FDCAN_CLASSIC_CAN;
    h.TxEventFifoControl = FDCAN_NO_TX_EVENTS;

    return (HAL_FDCAN_AddMessageToTxFifoQ(&hfdcan1, &h, (uint8_t *)data) == HAL_OK);
}

uint8_t SBusCAN_GetModuleId(void) { return s_module_id; }
uint8_t SBusCAN_GetEffectiveModuleId(void) { return sbus_effective_mid(); }
uint8_t SBusCAN_GetAutodetect(void) { return s_autodetect; }
uint32_t SBusCAN_GetBusOffCount(void) { return s_busoff_count; }
uint32_t SBusCAN_GetRecoverCount(void) { return s_recover_count; }
uint32_t SBusCAN_GetLastErrorFlags(void) { return s_last_error_flags; }

void SBusCAN_SetAutodetect(uint8_t en, uint32_t duration_ms)
{
    s_autodetect = (en != 0u) ? 1u : 0u;
    s_autodetect_until_ms = s_autodetect ? (app_now_ms() + duration_ms) : 0u;
}

void SBusCAN_SetBacklight(uint8_t bl_index_0_1, uint16_t value_0_1000)
{
    (void)bl_index_0_1;
    if (value_0_1000 > 1000u) value_0_1000 = 1000u;
    const uint8_t duty = (uint8_t)((value_0_1000 * 255u) / 1000u);
    pwm_set_u8(duty);
}

bool SBusCAN_SendIdentity(void)
{
    uint8_t data[8] = {0};
    data[0] = SBUS_TEL_IDENTITY;
    data[1] = sbus_effective_mid();
    data[2] = SBUS_FW_VER_MAJOR;
    data[3] = SBUS_FW_VER_MINOR;
    data[4] = SBUS_HW_REV;
    data[5] = SBUS_PROTO_VER;
    data[6] = (uint8_t)(s_uid_xor & 0xFFu);
    data[7] = (uint8_t)((s_uid_xor >> 8) & 0xFFu);
    return sbus_can_tx(sbus_tel_id(SBUS_PPP_STATUS), data, 8u);
}

bool SBusCAN_SendHeartbeat(void)
{
    uint8_t data[8] = {0};
    data[0] = SBUS_TEL_HEARTBEAT;
    data[1] = sbus_effective_mid();
    data[2] = s_autodetect;
    data[3] = 0u;
    data[4] = (uint8_t)(s_busoff_count & 0xFFu);
    data[5] = (uint8_t)((s_busoff_count >> 8) & 0xFFu);
    data[6] = (uint8_t)(s_last_error_flags & 0xFFu);
    data[7] = (uint8_t)((s_last_error_flags >> 8) & 0xFFu);
    return sbus_can_tx(sbus_tel_id(SBUS_PPP_STATUS), data, 8u);
}

bool SBusCAN_SendEncoderStep(uint8_t enc_index_0_5, int16_t delta)
{
    if (enc_index_0_5 >= SBUS_ENC_COUNT) return false;

    uint8_t data[8] = {0};
    data[0] = s_autodetect ? SBUS_TEL_AUTODETECT_EVENT : SBUS_TEL_ENCODER_STEP;
    data[1] = enc_index_0_5;
    data[2] = (uint8_t)(delta & 0xFF);
    data[3] = (uint8_t)((delta >> 8) & 0xFF);
    data[4] = 0x01u;
    return sbus_can_tx(sbus_tel_id(SBUS_PPP_ENCODER), data, 8u);
}

bool SBusCAN_SendDigitalChange(uint8_t index_0_31, uint8_t value_0_1)
{
    if (index_0_31 >= SBUS_DIG_COUNT) return false;

    uint8_t data[8] = {0};
    data[0] = s_autodetect ? SBUS_TEL_AUTODETECT_EVENT : SBUS_TEL_DIGITAL_CHANGE;
    data[1] = index_0_31;
    data[2] = (value_0_1 ? 1u : 0u);
    data[4] = 0x02u;
    return sbus_can_tx(sbus_tel_id(SBUS_PPP_DIGITAL), data, 8u);
}

bool SBusCAN_SendAnalogValue(uint8_t index_0_5, uint16_t raw_0_4095)
{
    if (index_0_5 >= SBUS_ANA_COUNT) return false;
    if (raw_0_4095 > 4095u) raw_0_4095 = 4095u;

    uint8_t data[8] = {0};
    data[0] = s_autodetect ? SBUS_TEL_AUTODETECT_EVENT : SBUS_TEL_ANALOG_VALUE;
    data[1] = index_0_5;
    data[2] = (uint8_t)(raw_0_4095 & 0xFFu);
    data[3] = (uint8_t)((raw_0_4095 >> 8) & 0xFFu);
    data[4] = 0x03u;
    return sbus_can_tx(sbus_tel_id(SBUS_PPP_ANALOG), data, 8u);
}

static void send_status_frame(void)
{
    uint8_t data[8] = {0};
    data[0] = SBUS_TEL_STATUS;
    data[1] = sbus_effective_mid();
    data[2] = s_autodetect;
    data[3] = 0u;
    data[4] = (uint8_t)(s_recover_count & 0xFFu);
    data[5] = (uint8_t)(s_busoff_count & 0xFFu);
    data[6] = (uint8_t)(s_last_error_flags & 0xFFu);
    data[7] = (uint8_t)((s_last_error_flags >> 8) & 0xFFu);
    (void)sbus_can_tx(sbus_tel_id(SBUS_PPP_STATUS), data, 8u);
}

static void send_error_frame(void)
{
    uint8_t data[8] = {0};
    data[0] = SBUS_TEL_ERROR;
    data[1] = (uint8_t)(s_last_error_flags & 0xFFu);
    data[2] = (uint8_t)((s_last_error_flags >> 8) & 0xFFu);
    data[3] = (uint8_t)((s_last_error_flags >> 16) & 0xFFu);
    data[4] = (uint8_t)((s_last_error_flags >> 24) & 0xFFu);
    data[5] = (uint8_t)(s_busoff_count & 0xFFu);
    data[6] = (uint8_t)(s_recover_count & 0xFFu);
    (void)sbus_can_tx(sbus_tel_id(SBUS_PPP_STATUS), data, 8u);
}

void SBusCAN_Service(void)
{
    const uint32_t now = app_now_ms();

    if (s_autodetect && (s_autodetect_until_ms != 0u) && ((int32_t)(now - s_autodetect_until_ms) >= 0)) {
        s_autodetect = 0u;
        s_autodetect_until_ms = 0u;
    }

    if ((int32_t)(now - s_last_status_ms) >= (int32_t)BOARD_CAN_STATUS_PERIOD_MS) {
        s_last_status_ms = now;
        send_status_frame();
    }

    if ((s_last_error_flags != 0u) && ((int32_t)(now - s_last_error_ms) >= (int32_t)BOARD_CAN_ERROR_PERIOD_MS)) {
        s_last_error_ms = now;
        send_error_frame();
    }

    if ((s_last_error_flags & FDCAN_IR_BO) == 0u) {
        s_next_tx_allowed_ms = 0u;
    }
}

static void on_cmd_ping(void)
{
    uint8_t data[8] = {0};
    data[0] = sbus_effective_mid();
    data[1] = SBUS_FW_VER_MAJOR;
    data[2] = SBUS_FW_VER_MINOR;
    data[3] = s_autodetect;
    data[4] = (uint8_t)(s_uid_xor & 0xFFu);
    data[5] = (uint8_t)((s_uid_xor >> 8) & 0xFFu);
    data[6] = (uint8_t)((s_uid_xor >> 16) & 0xFFu);
    data[7] = (uint8_t)((s_uid_xor >> 24) & 0xFFu);
    (void)sbus_can_tx(SBUS_CAN_ID_PONG, data, 8u);
}

static void on_cmd_set_backlight(const uint8_t *pl, uint8_t len)
{
    if (len < 1u) return;
    SBusCAN_SetBacklight(0u, (uint16_t)(((uint32_t)pl[0] * 1000u) / 255u));
}

static void on_cmd_set_autodetect(const uint8_t *pl, uint8_t len)
{
    if (len < 1u) return;
    uint32_t dur = BOARD_AUTODETECT_DEFAULT_MS;
    if (len >= 5u) {
        dur = (uint32_t)pl[1] | ((uint32_t)pl[2] << 8) | ((uint32_t)pl[3] << 16) | ((uint32_t)pl[4] << 24);
    }
    SBusCAN_SetAutodetect(pl[0], dur);
}

static void on_cmd_set_module_id(const uint8_t *pl, uint8_t len)
{
    if (len < 1u) return;
    const uint8_t new_id = pl[0];
    if (new_id > 63u) return;
    s_module_id = new_id;
    (void)sbus_nvm_store_module_id(new_id);
    (void)SBusCAN_SendIdentity();
}

void SBusCAN_HandleRx(uint16_t can_id_11bit, const uint8_t *data, uint8_t len)
{
    if (data == NULL) return;

    switch ((uint32_t)can_id_11bit) {
        case SBUS_CAN_ID_PING:
            on_cmd_ping();
            break;
        case SBUS_CAN_ID_SET_BACKLIGHT:
            on_cmd_set_backlight(data, len);
            break;
        case SBUS_CAN_ID_SET_AUTODETECT:
            on_cmd_set_autodetect(data, len);
            break;
        case SBUS_CAN_ID_SET_MODULE_ID:
            on_cmd_set_module_id(data, len);
            break;
        case SBUS_CAN_ID_REQ_IDENTITY:
            (void)SBusCAN_SendIdentity();
            break;
        default:
            break;
    }
}

void SBusCAN_OnErrorFromISR(uint32_t errorStatusIts)
{
    s_last_error_flags = errorStatusIts;

    if ((errorStatusIts & FDCAN_IR_BO) != 0u) {
        s_busoff_count++;
        s_next_tx_allowed_ms = app_now_ms() + BOARD_CAN_BUSOFF_RECOVER_DELAY_MS;
        s_recover_count++;
    }
}

void SBusCAN_Init(void)
{
    const uint32_t *uid = (const uint32_t *)UID_BASE;
    s_uid_xor = uid[0] ^ uid[1] ^ uid[2];

    s_module_id = SBUS_MODULE_ID_DEFAULT;
    (void)sbus_nvm_load_module_id(&s_module_id);
    if (s_module_id > 63u) s_module_id = SBUS_MODULE_ID_DEFAULT;

    s_autodetect = 0u;
    s_autodetect_until_ms = 0u;
    s_last_error_flags = 0u;
    s_busoff_count = 0u;
    s_recover_count = 0u;
    s_next_tx_allowed_ms = 0u;

    s_last_status_ms = app_now_ms();
    s_last_error_ms = app_now_ms();

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    pwm_set_u8((uint8_t)BOARD_BACKLIGHT_DEFAULT_U8);

    (void)SBusCAN_SendIdentity();
}

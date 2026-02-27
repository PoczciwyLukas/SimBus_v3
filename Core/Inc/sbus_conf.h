#ifndef SBUS_CONF_H
#define SBUS_CONF_H

#include <stdint.h>

/* ===== SimBus Protocol Rev A ===== */
#define SBUS_PROTO_VER                 (0x01u)
#define SBUS_FW_VER_MAJOR              (0x01u)
#define SBUS_FW_VER_MINOR              (0x00u)
#define SBUS_HW_REV                    (0x01u)

/* 0 = unassigned (commissioning mode) */
#define SBUS_MODULE_ID_DEFAULT         (0u)

/* ===== CAN ID format: PPP TT MMMMMM (11-bit) ===== */
#define SBUS_TT_TEL                    (0u)
#define SBUS_TT_CMD                    (1u)
#define SBUS_TT_BCAST                  (2u)

#define SBUS_PPP_ENCODER               (0u)
#define SBUS_PPP_DIGITAL               (1u)
#define SBUS_PPP_ANALOG                (2u)
#define SBUS_PPP_STATUS                (3u)

#define SBUS_MAKE_STDID(ppp, tt, mid) \
    ( (uint16_t)((((uint16_t)(ppp) & 0x7u) << 8) | (((uint16_t)(tt) & 0x3u) << 6) | ((uint16_t)(mid) & 0x3Fu)) )

#define SBUS_GET_MID(id)               ((uint8_t)((id) & 0x3Fu))

/* ===== Stable command IDs from master to slave ===== */
#define SBUS_CAN_ID_PING               (0x700u)
#define SBUS_CAN_ID_SET_BACKLIGHT      (0x701u)
#define SBUS_CAN_ID_SET_AUTODETECT     (0x702u)
#define SBUS_CAN_ID_SET_MODULE_ID      (0x703u)
#define SBUS_CAN_ID_REQ_IDENTITY       (0x704u)

/* ===== Stable response IDs from slave to master ===== */
#define SBUS_CAN_ID_PONG               (0x708u)

/* ===== TEL msg_kind (payload[0]) ===== */
#define SBUS_TEL_IDENTITY              (0x01u)
#define SBUS_TEL_DIGITAL_CHANGE        (0x10u)
#define SBUS_TEL_ENCODER_STEP          (0x11u)
#define SBUS_TEL_ANALOG_VALUE          (0x12u)
#define SBUS_TEL_BACKLIGHT_VALUE       (0x13u)
#define SBUS_TEL_HEARTBEAT             (0x1Fu)
#define SBUS_TEL_STATUS                (0x2Eu)
#define SBUS_TEL_ERROR                 (0x2Fu)
#define SBUS_TEL_AUTODETECT_EVENT      (0x3Eu)

/* ===== Limits ===== */
#define SBUS_ENC_COUNT                 (6u)
#define SBUS_ANA_COUNT                 (6u)
#define SBUS_BL_COUNT                  (2u)
#define SBUS_DIG_COUNT                 (32u)

#endif /* SBUS_CONF_H */

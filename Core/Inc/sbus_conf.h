#ifndef SBUS_CONF_H
#define SBUS_CONF_H

#include <stdint.h>

/* ===== SimBus Protocol Rev A ===== */
#define SBUS_PROTO_VER                 (0x01u)

/* ===== Firmware / Hardware revisions (diagnostyka) ===== */
#define SBUS_FW_VER                    (0x01u) /* FW major/minor – na razie byte */
#define SBUS_HW_REV                    (0x01u) /* RevA=0x01 */

/* ===== Addressing =====
 * Master has ID=0
 * Slave IDs: 1..63
 *
 * IMPORTANT:
 * - module_id is now loaded at runtime (from Flash NVM) and can be assigned by MASTER/PC
 *   using a broadcast command that targets a specific UID32.
 * - If NVM is empty, module_id defaults to SBUS_MODULE_ID_DEFAULT.
 * - Recommended: 0 ("unassigned") so the PC can assign IDs during commissioning.
 */
#define SBUS_MODULE_ID_DEFAULT         (0u)   /* 0..63 (0 = unassigned) */

/* ===== CAN ID format: PPP TT MMMMMM (11-bit) =====
 * PPP: bits 10..8
 * TT : bits 7..6
 * MID: bits 5..0
 */
#define SBUS_TT_TEL                    (0u)   /* 00 */
#define SBUS_TT_CMD                    (1u)   /* 01 */
#define SBUS_TT_BCAST                  (2u)   /* 10 */

#define SBUS_PPP_ENCODER               (0u)   /* 000 */
#define SBUS_PPP_DIGITAL               (1u)   /* 001 */
#define SBUS_PPP_ANALOG                (2u)   /* 010 */
#define SBUS_PPP_STATUS                (3u)   /* 011 */

/* ===== TEL msg_kind (payload[0]) ===== */
#define SBUS_TEL_IDENTITY              (0x01u) /* status: uid/fw/hw/proto */
#define SBUS_TEL_DIGITAL_CHANGE        (0x10u)
#define SBUS_TEL_ENCODER_STEP          (0x11u)
#define SBUS_TEL_ANALOG_VALUE          (0x12u)
#define SBUS_TEL_BACKLIGHT_VALUE       (0x13u) /* optional */
#define SBUS_TEL_HEARTBEAT             (0x1Fu)

/* ===== BCAST/CMD msg_kind (payload[0]) ===== */
#define SBUS_BCAST_STATE_REQUEST       (0x20u)
#define SBUS_BCAST_ASSIGN_MODULE_ID    (0x21u) /* [1..4]=UID32 LE, [5]=new_id (1..63) */

#define SBUS_CMD_SET_BACKLIGHT         (0x30u)
#define SBUS_CMD_REQ_IDENTITY          (0x31u)
#define SBUS_CMD_AUTODETECT_START      (0x32u)
#define SBUS_CMD_AUTODETECT_STOP       (0x33u)

/* ===== Limits ===== */
#define SBUS_ENC_COUNT                 (6u)
#define SBUS_ANA_COUNT                 (6u)
#define SBUS_BL_COUNT                  (2u)
#define SBUS_DIG_COUNT                 (32u)

#endif /* SBUS_CONF_H */

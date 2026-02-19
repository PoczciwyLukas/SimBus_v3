#ifndef APP_H
#define APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/*
 * SimBus 2.0 – SLAVE Rev A (STM32G0B1CBT6)
 * Production skeleton:
 * - cooperative main-loop architecture
 * - event queue
 * - periodic scheduler
 * - ISR-safe event posting
 */

typedef enum
{
    APP_EVT_NONE = 0,

    /* Inputs */
    APP_EVT_ENCODER_STEP,     // a=enc_id (1..6), b=delta(+/-)
    APP_EVT_DIGITAL_CHANGE,   // a=io_id (0..31), b=value(0/1)
    APP_EVT_ANALOG_VALUE,     // a=ch_id (0..5), b=value(0..4095)

    /* Periodic tasks */
    APP_EVT_HEARTBEAT_DUE,    // periodic heartbeat tick
    APP_EVT_MCP_POLL_DUE,     // poll MCP23017
    APP_EVT_ADC_PROCESS_DUE,  // process ADC DMA buffer
    APP_EVT_CAN_SERVICE_DUE,  // CAN service tick (bus-off recovery etc.)

    /* CAN */
    APP_EVT_CAN_RX,           // RX FIFO0 pending

    APP_EVT__COUNT
} app_event_type_t;

typedef struct
{
    app_event_type_t type;
    uint32_t ts_ms;
    uint16_t a;
    int16_t  b;
    uint16_t flags;
} app_event_t;

void App_Init(void);
void App_Run(void);

bool App_PostEventFromISR(app_event_type_t type, uint16_t a, int16_t b, uint16_t flags);
bool App_PostEvent(app_event_type_t type, uint16_t a, int16_t b, uint16_t flags);

#ifdef __cplusplus
}
#endif

#endif /* APP_H */

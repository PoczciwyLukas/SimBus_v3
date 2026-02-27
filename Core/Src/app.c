#include "app.h"

#include "main.h"
#include "fdcan.h"
#include "stm32g0xx_hal.h"

#include "sbus_can.h"
#include "sbus_conf.h"

#include "mcp23017.h"
#include "adc_app.h"

#include "board_conf.h"

#if defined(HAL_IWDG_MODULE_ENABLED)
#include "stm32g0xx_hal_iwdg.h"
#define APP_HAS_HAL_IWDG 1
#else
#define APP_HAS_HAL_IWDG 0
#endif

#include <string.h>

#define APP_EVENT_QUEUE_SIZE     128u
#define APP_MAX_EVENTS_PER_RUN    32u

static volatile uint16_t s_q_head = 0;
static volatile uint16_t s_q_tail = 0;
static app_event_t s_q[APP_EVENT_QUEUE_SIZE];

static struct
{
    uint32_t boot_ms;
    volatile uint32_t dropped_events;
} s_app;

static uint8_t s_wdg_inited = 0u;
#if APP_HAS_HAL_IWDG
static IWDG_HandleTypeDef s_hiwdg;
#endif

static inline uint32_t app_now_ms(void) { return HAL_GetTick(); }

static inline void app_crit_enter(uint32_t *primask)
{
    *primask = __get_PRIMASK();
    __disable_irq();
}
static inline void app_crit_exit(uint32_t primask)
{
    if ((primask & 0x1u) == 0u) __enable_irq();
}

static inline void app_inc_dropped(void)
{
    uint32_t primask;
    app_crit_enter(&primask);
    s_app.dropped_events++;
    app_crit_exit(primask);
}

static bool app_q_push(app_event_type_t type, uint16_t a, int16_t b, uint16_t flags, uint32_t ts_ms)
{
    uint32_t primask;
    app_crit_enter(&primask);

    const uint16_t head = s_q_head;
    const uint16_t next = (uint16_t)((head + 1u) % APP_EVENT_QUEUE_SIZE);

    if (next == s_q_tail) {
        app_crit_exit(primask);
        return false;
    }

    s_q[head].type  = type;
    s_q[head].ts_ms = ts_ms;
    s_q[head].a     = a;
    s_q[head].b     = b;
    s_q[head].flags = flags;

    s_q_head = next;

    app_crit_exit(primask);
    return true;
}

static bool app_q_pop(app_event_t *out)
{
    uint32_t primask;
    app_crit_enter(&primask);

    if (s_q_tail == s_q_head) {
        app_crit_exit(primask);
        return false;
    }

    const uint16_t tail = s_q_tail;
    *out = s_q[tail];
    s_q_tail = (uint16_t)((tail + 1u) % APP_EVENT_QUEUE_SIZE);

    app_crit_exit(primask);
    return true;
}


static void app_watchdog_init(void)
{
#if APP_HAS_HAL_IWDG
    s_hiwdg.Instance = IWDG;
    s_hiwdg.Init.Prescaler = IWDG_PRESCALER_64;
    s_hiwdg.Init.Window = 4095u;
    s_hiwdg.Init.Reload = (uint32_t)(((uint64_t)BOARD_WATCHDOG_TIMEOUT_MS * (LSI_VALUE / 64u)) / 1000u);
    if (s_hiwdg.Init.Reload > 4095u) s_hiwdg.Init.Reload = 4095u;
    if (s_hiwdg.Init.Reload < 50u) s_hiwdg.Init.Reload = 50u;

    if (HAL_IWDG_Init(&s_hiwdg) == HAL_OK) {
        s_wdg_inited = 1u;
    }
#else
    s_wdg_inited = 0u;
#endif
}

static inline void app_watchdog_kick(void)
{
#if APP_HAS_HAL_IWDG
    if (s_wdg_inited != 0u) {
        (void)HAL_IWDG_Refresh(&s_hiwdg);
    }
#endif
}

typedef struct
{
    uint32_t period_ms;
    uint32_t next_due_ms;
    app_event_type_t evt;
} app_task_t;

static app_task_t s_tasks[] =
{
    { BOARD_PERIOD_HEARTBEAT_MS,   0u, APP_EVT_HEARTBEAT_DUE   },
    { BOARD_PERIOD_MCP_POLL_MS,    0u, APP_EVT_MCP_POLL_DUE    },
    { BOARD_PERIOD_ADC_PROC_MS,    0u, APP_EVT_ADC_PROCESS_DUE },
    { BOARD_PERIOD_CAN_SERVICE_MS, 0u, APP_EVT_CAN_SERVICE_DUE },
};

static void app_sched_init(uint32_t now)
{
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_tasks) / sizeof(s_tasks[0])); i++) {
        s_tasks[i].next_due_ms = now + s_tasks[i].period_ms;
    }
}

static void app_sched_step(uint32_t now)
{
    for (uint32_t i = 0; i < (uint32_t)(sizeof(s_tasks) / sizeof(s_tasks[0])); i++) {
        if ((int32_t)(now - s_tasks[i].next_due_ms) >= 0) {
            s_tasks[i].next_due_ms += s_tasks[i].period_ms;
            (void)app_q_push(s_tasks[i].evt, 0u, 0, 0u, now);
        }
    }
}

static void on_can_rx(void)
{
    FDCAN_RxHeaderTypeDef rxHeader;
    uint8_t rxData[8];

    while (HAL_FDCAN_GetRxFifoFillLevel(&hfdcan1, FDCAN_RX_FIFO0) > 0u)
    {
        if (HAL_FDCAN_GetRxMessage(&hfdcan1, FDCAN_RX_FIFO0, &rxHeader, rxData) != HAL_OK) {
            break;
        }

        if (rxHeader.IdType != FDCAN_STANDARD_ID) continue;
        if (rxHeader.RxFrameType != FDCAN_DATA_FRAME) continue;
        uint8_t dlc = (uint8_t)((rxHeader.DataLength >> 16) & 0x0Fu);
        if (dlc > 8u) dlc = 8u;

        const uint16_t can_id = (uint16_t)(rxHeader.Identifier & 0x7FFu);
        SBusCAN_HandleRx(can_id, rxData, dlc);
    }
}

static void on_heartbeat_due(void)    { (void)SBusCAN_SendHeartbeat(); }
static void on_mcp_poll_due(void)     { MCP_Poll(); }
static void on_adc_process_due(void)  { ADCAPP_Process(); }
static void on_can_service_due(void)  { SBusCAN_Service(); }

static void on_encoder_step(uint16_t enc_id_1_6, int16_t delta)
{
    if (enc_id_1_6 < 1u || enc_id_1_6 > 6u) return;
    uint8_t idx = (uint8_t)(enc_id_1_6 - 1u);
    (void)SBusCAN_SendEncoderStep(idx, delta);
}

static void on_digital_change(uint16_t index, int16_t value)
{
    if (index > 31u) return;
    (void)SBusCAN_SendDigitalChange((uint8_t)index, (value ? 1u : 0u));
}

static void on_analog_value(uint16_t index, int16_t value)
{
    if (index >= 6u) return;
    uint16_t v = (value < 0) ? 0u : (uint16_t)value;
    if (v > 4095u) v = 4095u;
    (void)SBusCAN_SendAnalogValue((uint8_t)index, v);
}

static void app_handle_event(const app_event_t *e)
{
    switch (e->type)
    {
        case APP_EVT_HEARTBEAT_DUE:    on_heartbeat_due(); break;
        case APP_EVT_MCP_POLL_DUE:     on_mcp_poll_due(); break;
        case APP_EVT_ADC_PROCESS_DUE:  on_adc_process_due(); break;
        case APP_EVT_CAN_SERVICE_DUE:  on_can_service_due(); break;

        case APP_EVT_ENCODER_STEP:     on_encoder_step(e->a, e->b); break;
        case APP_EVT_DIGITAL_CHANGE:   on_digital_change(e->a, e->b); break;
        case APP_EVT_ANALOG_VALUE:     on_analog_value(e->a, e->b); break;

        case APP_EVT_CAN_RX:           on_can_rx(); break;
        default: break;
    }
}

void App_Init(void)
{
    const uint32_t now = app_now_ms();

    s_q_head = 0;
    s_q_tail = 0;

    s_app.boot_ms = now;
    s_app.dropped_events = 0;

    app_sched_init(now);

    app_watchdog_init();

    SBusCAN_Init();
    MCP_Init();
    ADCAPP_Init();
}

void App_Run(void)
{
    const uint32_t now = app_now_ms();

    app_sched_step(now);

    for (uint32_t i = 0; i < APP_MAX_EVENTS_PER_RUN; i++) {
        app_event_t e;
        if (!app_q_pop(&e)) break;
        app_handle_event(&e);
        app_watchdog_kick();
    }

    app_watchdog_kick();
}

bool App_PostEventFromISR(app_event_type_t type, uint16_t a, int16_t b, uint16_t flags)
{
    const uint32_t ts = app_now_ms();
    const bool ok = app_q_push(type, a, b, flags, ts);
    if (!ok) app_inc_dropped();
    return ok;
}

bool App_PostEvent(app_event_type_t type, uint16_t a, int16_t b, uint16_t flags)
{
    const uint32_t ts = app_now_ms();
    const bool ok = app_q_push(type, a, b, flags, ts);
    if (!ok) app_inc_dropped();
    return ok;
}

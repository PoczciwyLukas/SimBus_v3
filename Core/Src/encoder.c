#include "encoder.h"

#include "app.h"
#include "gpio.h"
#include "stm32g0xx_hal.h"

/* ===== Rev A encoder pin map =====
   ENC1: PB0,  PB1
   ENC2: PB2,  PB10
   ENC3: PB11, PB12
   ENC4: PB3,  PB4
   ENC5: PB5,  PB6
   ENC6: PB14, PB15
*/

#define ENC_COUNT                6u

/* ile przejść kwadraturowych traktujemy jako 1 “klik”
   typowo 4 (full cycle). Jak kiedyś wyjdzie, że masz enkodery 2-step,
   zmienimy na 2 — ale produkcyjnie 4 jest najbezpieczniejsze. */
#define ENC_TRANSITIONS_PER_STEP 4

typedef struct
{
    GPIO_TypeDef *portA;
    uint16_t pinA;
    GPIO_TypeDef *portB;
    uint16_t pinB;

    uint8_t  prev_ab;        /* 2-bit state */
    int8_t   accum;          /* sum przejść: -4..+4 */
    uint32_t last_emit_ms;   /* do akceleracji (czas między “klikami”) */
} enc_t;

static enc_t s_enc[ENC_COUNT];

/* Quadrature transition table:
   index = (prev<<2) | curr, value = -1/0/+1 */
static const int8_t s_qdec_table[16] =
{
    0,  +1, -1,  0,
   -1,  0,  0, +1,
   +1,  0,  0, -1,
    0, -1, +1,  0
};

static inline uint8_t read_ab(const enc_t *e)
{
    uint8_t a = (HAL_GPIO_ReadPin(e->portA, e->pinA) == GPIO_PIN_SET) ? 1u : 0u;
    uint8_t b = (HAL_GPIO_ReadPin(e->portB, e->pinB) == GPIO_PIN_SET) ? 1u : 0u;
    return (uint8_t)((a << 1) | b);
}

static inline int16_t apply_accel(uint32_t dt_ms, int16_t base_step)
{
    /* Rev A accel:
       dt > 80 ms  -> n=1
       35..80 ms   -> n=2
       < 35 ms     -> n=4
    */
    int16_t mul = 1;

    if (dt_ms < 35u) mul = 4;
    else if (dt_ms < 80u) mul = 2;
    else mul = 1;

    return (int16_t)(base_step * mul);
}

static void emit_step(uint8_t enc_idx, int16_t step_pm1)
{
    uint32_t now = HAL_GetTick();
    uint32_t dt  = now - s_enc[enc_idx].last_emit_ms;
    s_enc[enc_idx].last_emit_ms = now;

    int16_t out = apply_accel(dt, step_pm1);

    /* enc_id w APP: 1..6 */
    (void)App_PostEventFromISR(APP_EVT_ENCODER_STEP, (uint16_t)(enc_idx + 1u), out, 0u);
}

static void handle_encoder(uint8_t enc_idx)
{
    enc_t *e = &s_enc[enc_idx];

    uint8_t curr = read_ab(e);
    uint8_t prev = e->prev_ab;

    uint8_t key = (uint8_t)((prev << 2) | curr);
    int8_t  step = s_qdec_table[key];

    e->prev_ab = curr;

    if (step == 0) return;

    /* akumuluj przejścia */
    int16_t a = (int16_t)e->accum + (int16_t)step;

    /* clamp safety */
    if (a > (int16_t)ENC_TRANSITIONS_PER_STEP) a = (int16_t)ENC_TRANSITIONS_PER_STEP;
    if (a < -(int16_t)ENC_TRANSITIONS_PER_STEP) a = -(int16_t)ENC_TRANSITIONS_PER_STEP;

    e->accum = (int8_t)a;

    if (e->accum >= (int8_t)ENC_TRANSITIONS_PER_STEP) {
        e->accum = 0;
        emit_step(enc_idx, +1);
    } else if (e->accum <= -(int8_t)ENC_TRANSITIONS_PER_STEP) {
        e->accum = 0;
        emit_step(enc_idx, -1);
    }
}

void Encoder_Init(void)
{
    /* ENC1 */
    s_enc[0].portA = GPIOB; s_enc[0].pinA = GPIO_PIN_0;
    s_enc[0].portB = GPIOB; s_enc[0].pinB = GPIO_PIN_1;

    /* ENC2 */
    s_enc[1].portA = GPIOB; s_enc[1].pinA = GPIO_PIN_2;
    s_enc[1].portB = GPIOB; s_enc[1].pinB = GPIO_PIN_10;

    /* ENC3 */
    s_enc[2].portA = GPIOB; s_enc[2].pinA = GPIO_PIN_11;
    s_enc[2].portB = GPIOB; s_enc[2].pinB = GPIO_PIN_12;

    /* ENC4 */
    s_enc[3].portA = GPIOB; s_enc[3].pinA = GPIO_PIN_3;
    s_enc[3].portB = GPIOB; s_enc[3].pinB = GPIO_PIN_4;

    /* ENC5 */
    s_enc[4].portA = GPIOB; s_enc[4].pinA = GPIO_PIN_5;
    s_enc[4].portB = GPIOB; s_enc[4].pinB = GPIO_PIN_6;

    /* ENC6 */
    s_enc[5].portA = GPIOB; s_enc[5].pinA = GPIO_PIN_14;
    s_enc[5].portB = GPIOB; s_enc[5].pinB = GPIO_PIN_15;

    uint32_t now = HAL_GetTick();

    for (uint8_t i = 0; i < ENC_COUNT; i++) {
        s_enc[i].prev_ab = read_ab(&s_enc[i]);
        s_enc[i].accum = 0;
        s_enc[i].last_emit_ms = now;
    }
}

void Encoder_OnExti(uint16_t gpio_pin)
{
    /* Route pin -> encoder index.
       Zawsze czytamy oba A/B, bez zgadywania które zbocze przyszło. */
    switch (gpio_pin)
    {
        case GPIO_PIN_0:
        case GPIO_PIN_1:
            handle_encoder(0);
            break;

        case GPIO_PIN_2:
        case GPIO_PIN_10:
            handle_encoder(1);
            break;

        case GPIO_PIN_11:
        case GPIO_PIN_12:
            handle_encoder(2);
            break;

        case GPIO_PIN_3:
        case GPIO_PIN_4:
            handle_encoder(3);
            break;

        case GPIO_PIN_5:
        case GPIO_PIN_6:
            handle_encoder(4);
            break;

        case GPIO_PIN_14:
        case GPIO_PIN_15:
            handle_encoder(5);
            break;

        default:
            break;
    }
}

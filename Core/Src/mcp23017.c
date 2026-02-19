#include "mcp23017.h"

#include "main.h"
#include "app.h"
#include "i2c_resilient.h"

#include <string.h>

/* ===================== CONFIG (NO ASSUMPTIONS HIDDEN) ===================== */
#define MCP1_ADDR_7BIT      0x20u
#define MCP2_ADDR_7BIT      0x21u

/* U Ciebie typowo: pull-up + switch do GND.
   RAW: puszczone=1, wciśnięte=0.
   Chcemy produkcyjnie: 1 = wciśnięte => invert wszystkich bitów. */
#define MCP_INVERT_MASK     (0xFFFFFFFFu)

/* Debounce: poll ~5ms, N samples -> N*5ms */
#define MCP_DEBOUNCE_COUNT_MAX   3u

/* ===================== MCP23017 register map (BANK=0) ===================== */
#define MCP_IODIRA          0x00u
#define MCP_IODIRB          0x01u
#define MCP_GPPUA           0x0Cu
#define MCP_GPPUB           0x0Du
#define MCP_GPIOA           0x12u

/* ===================== INTERNAL STATE ===================== */
#define MCP1_ADDR_8BIT      ((uint8_t)(MCP1_ADDR_7BIT << 1))
#define MCP2_ADDR_8BIT      ((uint8_t)(MCP2_ADDR_7BIT << 1))

static uint8_t  s_inited = 0;

static uint32_t s_raw = 0;
static uint32_t s_stable = 0;
static uint8_t  s_deb[32];

static uint32_t s_i2c_err = 0;

/* ===================== I2C helpers ===================== */
static HAL_StatusTypeDef mcp_write_u8(uint8_t addr8, uint8_t reg, uint8_t val)
{
    HAL_StatusTypeDef st = I2C1_MemWrite8_Retry(addr8, reg, &val, 1u);
    if (st != HAL_OK) s_i2c_err++;
    return st;
}

static HAL_StatusTypeDef mcp_read_gpio16(uint8_t addr8, uint16_t *out)
{
    uint8_t buf[2] = {0};
    HAL_StatusTypeDef st = I2C1_MemRead8_Retry(addr8, MCP_GPIOA, buf, 2u);
    if (st != HAL_OK) {
        s_i2c_err++;
        return st;
    }

    *out = (uint16_t)((uint16_t)buf[0] | ((uint16_t)buf[1] << 8));
    return HAL_OK;
}

/* ===================== MCP config ===================== */
static void mcp_config_inputs_pullups(uint8_t addr8)
{
    if (mcp_write_u8(addr8, MCP_IODIRA, 0xFFu) != HAL_OK) Error_Handler();
    if (mcp_write_u8(addr8, MCP_IODIRB, 0xFFu) != HAL_OK) Error_Handler();

    if (mcp_write_u8(addr8, MCP_GPPUA, 0xFFu) != HAL_OK) Error_Handler();
    if (mcp_write_u8(addr8, MCP_GPPUB, 0xFFu) != HAL_OK) Error_Handler();
}

static inline uint32_t mcp_apply_invert(uint32_t v32)
{
    return (v32 ^ MCP_INVERT_MASK);
}

/* ===================== Public API ===================== */
void MCP_Init(void)
{
    if (s_inited) return;

    memset(s_deb, 0, sizeof(s_deb));

    mcp_config_inputs_pullups(MCP1_ADDR_8BIT);
    mcp_config_inputs_pullups(MCP2_ADDR_8BIT);

    uint16_t v1 = 0, v2 = 0;

    if (mcp_read_gpio16(MCP1_ADDR_8BIT, &v1) != HAL_OK) Error_Handler();
    if (mcp_read_gpio16(MCP2_ADDR_8BIT, &v2) != HAL_OK) Error_Handler();

    uint32_t v = ((uint32_t)v2 << 16) | (uint32_t)v1;
    v = mcp_apply_invert(v);

    s_raw = v;
    s_stable = v;

    s_inited = 1;
}

void MCP_Poll(void)
{
    if (!s_inited) return;

    uint16_t v1 = 0, v2 = 0;

    /* jak read fail -> pomijamy tick, nie zmieniamy stable */
    if (mcp_read_gpio16(MCP1_ADDR_8BIT, &v1) != HAL_OK) return;
    if (mcp_read_gpio16(MCP2_ADDR_8BIT, &v2) != HAL_OK) return;

    uint32_t v = ((uint32_t)v2 << 16) | (uint32_t)v1;
    v = mcp_apply_invert(v);

    s_raw = v;

    for (uint8_t bit = 0; bit < 32u; bit++)
    {
        const uint32_t mask = (1u << bit);
        const uint8_t sample = (v & mask) ? 1u : 0u;
        const uint8_t stable = (s_stable & mask) ? 1u : 0u;

        if (sample == stable)
        {
            if (s_deb[bit] > 0u) s_deb[bit]--;
            continue;
        }

        if (s_deb[bit] < MCP_DEBOUNCE_COUNT_MAX) s_deb[bit]++;

        if (s_deb[bit] >= MCP_DEBOUNCE_COUNT_MAX)
        {
            s_deb[bit] = 0u;

            if (sample) s_stable |= mask;
            else        s_stable &= ~mask;

            (void)App_PostEvent(APP_EVT_DIGITAL_CHANGE,
                                (uint16_t)bit,
                                (int16_t)(sample ? 1 : 0),
                                0u);
        }
    }
}

void MCP_EmitSnapshot(void)
{
    if (!s_inited) return;

    for (uint8_t bit = 0; bit < 32u; bit++)
    {
        const uint32_t mask = (1u << bit);
        const uint8_t value = (s_stable & mask) ? 1u : 0u;

        (void)App_PostEvent(APP_EVT_DIGITAL_CHANGE,
                            (uint16_t)bit,
                            (int16_t)(value ? 1 : 0),
                            0u);
    }
}

/* ===================== Diagnostics ===================== */
uint32_t MCP_GetStableState(void) { return s_stable; }
uint32_t MCP_GetRawState(void)    { return s_raw; }
uint32_t MCP_GetI2CErrCount(void) { return s_i2c_err; }

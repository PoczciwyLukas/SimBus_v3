#include "i2c_resilient.h"

#include "i2c.h"
#include "gpio.h"
#include "stm32g0xx_hal.h"

/* PB8=SCL, PB9=SDA wg Twojego CubeMX i2c.c */
#define I2C1_SCL_PORT        GPIOB
#define I2C1_SCL_PIN         GPIO_PIN_8
#define I2C1_SDA_PORT        GPIOB
#define I2C1_SDA_PIN         GPIO_PIN_9

#define I2C_RETRY_COUNT      3u
#define I2C_OP_TIMEOUT_MS    5u

static void delay_short(void)
{
    /* krótkie opóźnienie (kilka us) */
    for (volatile uint32_t i = 0; i < 200u; i++) { __NOP(); }
}

void I2C1_BusRecover(void)
{
    /* 1) wyłącz I2C */
    (void)HAL_I2C_DeInit(&hi2c1);

    /* 2) ustaw piny jako GPIO OD */
    GPIO_InitTypeDef gi = {0};
    gi.Mode  = GPIO_MODE_OUTPUT_OD;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;

    gi.Pin = I2C1_SCL_PIN;
    HAL_GPIO_Init(I2C1_SCL_PORT, &gi);

    gi.Pin = I2C1_SDA_PIN;
    HAL_GPIO_Init(I2C1_SDA_PORT, &gi);

    /* puść linie (OD -> stan wysoki = nie ściągamy) */
    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);
    delay_short();

    /* 9 impulsów SCL */
    for (uint8_t i = 0; i < 9u; i++) {
        HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_RESET);
        delay_short();
        HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
        delay_short();
    }

    /* STOP: SDA low -> SCL high -> SDA high */
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_RESET);
    delay_short();
    HAL_GPIO_WritePin(I2C1_SCL_PORT, I2C1_SCL_PIN, GPIO_PIN_SET);
    delay_short();
    HAL_GPIO_WritePin(I2C1_SDA_PORT, I2C1_SDA_PIN, GPIO_PIN_SET);
    delay_short();

    /* 3) wróć do konfiguracji CubeMX */
    MX_I2C1_Init();
}

static HAL_StatusTypeDef mem_write(uint16_t devAddr8, uint16_t reg, const uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Write(&hi2c1, devAddr8, reg, I2C_MEMADD_SIZE_8BIT,
                            (uint8_t*)data, len, I2C_OP_TIMEOUT_MS);
}

static HAL_StatusTypeDef mem_read(uint16_t devAddr8, uint16_t reg, uint8_t *data, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, devAddr8, reg, I2C_MEMADD_SIZE_8BIT,
                            data, len, I2C_OP_TIMEOUT_MS);
}

HAL_StatusTypeDef I2C1_MemWrite8_Retry(uint16_t devAddr8, uint16_t reg, const uint8_t *data, uint16_t len)
{
    for (uint8_t try = 0; try < I2C_RETRY_COUNT; try++) {
        HAL_StatusTypeDef st = mem_write(devAddr8, reg, data, len);
        if (st == HAL_OK) return HAL_OK;
        I2C1_BusRecover();
    }
    return HAL_ERROR;
}

HAL_StatusTypeDef I2C1_MemRead8_Retry(uint16_t devAddr8, uint16_t reg, uint8_t *data, uint16_t len)
{
    for (uint8_t try = 0; try < I2C_RETRY_COUNT; try++) {
        HAL_StatusTypeDef st = mem_read(devAddr8, reg, data, len);
        if (st == HAL_OK) return HAL_OK;
        I2C1_BusRecover();
    }
    return HAL_ERROR;
}

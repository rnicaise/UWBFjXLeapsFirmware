/*
 * accel.c - accelerometer backend selection
 *
 * Pyro/GenA boxes expose an external BMI323 over SPI. Legacy DWM3001C boxes use
 * the module's LIS2DH12 over I2C. The selected CMake preset fixes the backend
 * so a legacy build never probes BMI323 and a GenA build never falls back to LIS.
 */

#include "accel.h"

#include <nrf.h>
#include <nrf_delay.h>
#include <nrf_gpio.h>

typedef enum {
    ACCEL_BACKEND_NONE = 0,
    ACCEL_BACKEND_BMI323,
    ACCEL_BACKEND_LIS2DH12
} accel_backend_t;

static accel_backend_t active_backend = ACCEL_BACKEND_NONE;

#if defined(__GNUC__)
#define ACCEL_MAYBE_UNUSED __attribute__((unused))
#else
#define ACCEL_MAYBE_UNUSED
#endif

/* -------------------------------------------------------------------------
 * BMI323 backend, SPI 4-wire on pyro boxes.
 * ------------------------------------------------------------------------- */

#define BMI323_SCLK_PIN NRF_GPIO_PIN_MAP(0, 17)
#define BMI323_MOSI_PIN NRF_GPIO_PIN_MAP(0, 20)
#define BMI323_MISO_PIN NRF_GPIO_PIN_MAP(0, 21)
#define BMI323_CS_PIN   NRF_GPIO_PIN_MAP(0, 11)
#define BMI323_INT1_PIN NRF_GPIO_PIN_MAP(1, 8)
#define BMI323_INT2_PIN NRF_GPIO_PIN_MAP(0, 6)

#define BMI323_SPI NRF_SPIM2
#define BMI323_SPI_TIMEOUT 640000u

#define BMI323_REG_CHIP_ID    0x00u
#define BMI323_REG_ERR_REG    0x01u
#define BMI323_REG_ACC_DATA_X 0x03u
#define BMI323_REG_ACC_CONF   0x20u

#define BMI323_CHIP_ID_EXPECTED 0x43u
#define BMI323_ACC_CONF_VALUE 0x4027u
#define BMI323_ERR_FATAL_MASK 0x0001u

static uint8_t bmi323_read_addr_flag = 0x80u;
static uint8_t bmi323_read_data_lsb_idx = 2u;
static bool bmi323_use_bitbang = false;

static void bmi323_spi_set_mode(uint32_t cpol, uint32_t cpha)
{
    BMI323_SPI->CONFIG =
        (SPIM_CONFIG_ORDER_MsbFirst << SPIM_CONFIG_ORDER_Pos) |
        (cpol << SPIM_CONFIG_CPOL_Pos) |
        (cpha << SPIM_CONFIG_CPHA_Pos);
}

static void bmi323_spi_select_interface(void)
{
    nrf_gpio_pin_clear(BMI323_CS_PIN);
    nrf_delay_us(5u);
    nrf_gpio_pin_set(BMI323_CS_PIN);
    nrf_delay_us(250u);
}

static void bmi323_bitbang_init(void)
{
    NRF_SPIM2->ENABLE = 0;
    NRF_SPI2->ENABLE = 0;

    nrf_gpio_cfg_output(BMI323_CS_PIN);
    nrf_gpio_cfg_output(BMI323_SCLK_PIN);
    nrf_gpio_cfg_output(BMI323_MOSI_PIN);
    nrf_gpio_cfg_input(BMI323_MISO_PIN, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_input(BMI323_INT1_PIN, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_input(BMI323_INT2_PIN, NRF_GPIO_PIN_NOPULL);

    nrf_gpio_pin_clear(BMI323_SCLK_PIN);
    nrf_gpio_pin_set(BMI323_MOSI_PIN);
    nrf_gpio_pin_set(BMI323_CS_PIN);

    bmi323_use_bitbang = true;
    bmi323_spi_select_interface();
}

static uint8_t bmi323_bitbang_byte(uint8_t tx)
{
    uint8_t rx = 0u;

    for (uint8_t bit = 0u; bit < 8u; bit++)
    {
        if ((tx & 0x80u) != 0u)
        {
            nrf_gpio_pin_set(BMI323_MOSI_PIN);
        }
        else
        {
            nrf_gpio_pin_clear(BMI323_MOSI_PIN);
        }

        nrf_delay_us(2u);
        nrf_gpio_pin_set(BMI323_SCLK_PIN);
        nrf_delay_us(2u);

        rx <<= 1;
        if (nrf_gpio_pin_read(BMI323_MISO_PIN) != 0u)
        {
            rx |= 1u;
        }

        nrf_gpio_pin_clear(BMI323_SCLK_PIN);
        nrf_delay_us(2u);
        tx <<= 1;
    }

    return rx;
}

static bool bmi323_bitbang_xfer(const uint8_t *tx, uint8_t *rx, uint8_t len)
{
    static uint8_t rx_sink[16];

    if ((tx == NULL) || (len == 0u) || (len > sizeof(rx_sink)))
    {
        return false;
    }

    if (rx == NULL)
    {
        rx = rx_sink;
    }

    for (uint8_t i = 0u; i < len; i++)
    {
        rx[i] = bmi323_bitbang_byte(tx[i]);
    }

    return true;
}

static void bmi323_spi_init(void)
{
    NRF_SPIM2->ENABLE = 0;
    NRF_SPI2->ENABLE = 0;
    bmi323_use_bitbang = false;

    nrf_gpio_cfg_output(BMI323_CS_PIN);
    nrf_gpio_pin_set(BMI323_CS_PIN);
    nrf_gpio_cfg_input(BMI323_INT1_PIN, NRF_GPIO_PIN_NOPULL);
    nrf_gpio_cfg_input(BMI323_INT2_PIN, NRF_GPIO_PIN_NOPULL);

    BMI323_SPI->PSEL.SCK = BMI323_SCLK_PIN;
    BMI323_SPI->PSEL.MOSI = BMI323_MOSI_PIN;
    BMI323_SPI->PSEL.MISO = BMI323_MISO_PIN;
    BMI323_SPI->FREQUENCY = SPIM_FREQUENCY_FREQUENCY_M1;
    bmi323_spi_set_mode(SPIM_CONFIG_CPOL_ActiveHigh, SPIM_CONFIG_CPHA_Leading);
    BMI323_SPI->ORC = 0xFFu;
    BMI323_SPI->ENABLE = SPIM_ENABLE_ENABLE_Enabled;

    bmi323_spi_select_interface();
}

static bool bmi323_spi_xfer(const uint8_t *tx, uint8_t *rx, uint8_t len)
{
    static uint8_t rx_sink[16];
    volatile uint32_t timeout = BMI323_SPI_TIMEOUT;

    if (bmi323_use_bitbang)
    {
        return bmi323_bitbang_xfer(tx, rx, len);
    }

    if ((tx == NULL) || (len == 0u) || (len > sizeof(rx_sink)))
    {
        return false;
    }

    if (rx == NULL)
    {
        rx = rx_sink;
    }

    BMI323_SPI->TXD.PTR = (uint32_t)tx;
    BMI323_SPI->TXD.MAXCNT = len;
    BMI323_SPI->RXD.PTR = (uint32_t)rx;
    BMI323_SPI->RXD.MAXCNT = len;
    BMI323_SPI->EVENTS_END = 0;
    BMI323_SPI->EVENTS_STOPPED = 0;
    BMI323_SPI->TASKS_START = 1;

    while ((BMI323_SPI->EVENTS_END == 0u) && (--timeout > 0u)) { }

    BMI323_SPI->TASKS_STOP = 1;
    timeout = BMI323_SPI_TIMEOUT;
    while ((BMI323_SPI->EVENTS_STOPPED == 0u) && (--timeout > 0u)) { }
    BMI323_SPI->EVENTS_STOPPED = 0;

    return BMI323_SPI->EVENTS_END != 0u;
}

static bool bmi323_try_read_chip_id_mode(uint8_t addr_flag, uint8_t data_lsb_idx)
{
    uint8_t tx[5] = { 0 };
    uint8_t rx[5] = { 0 };
    uint16_t reg_word;

    tx[0] = (uint8_t)(BMI323_REG_CHIP_ID | addr_flag);
    tx[1] = 0xFFu;
    tx[2] = 0xFFu;
    tx[3] = 0xFFu;
    tx[4] = 0xFFu;

    nrf_gpio_pin_clear(BMI323_CS_PIN);
    nrf_delay_us(1u);
    if (!bmi323_spi_xfer(tx, rx, sizeof(tx)))
    {
        nrf_gpio_pin_set(BMI323_CS_PIN);
        nrf_delay_us(2u);
        return false;
    }
    nrf_delay_us(1u);
    nrf_gpio_pin_set(BMI323_CS_PIN);
    nrf_delay_us(2u);

    if ((data_lsb_idx + 1u) >= sizeof(rx))
    {
        return false;
    }

    reg_word = (uint16_t)rx[data_lsb_idx] | ((uint16_t)rx[data_lsb_idx + 1u] << 8);
    bmi323_read_addr_flag = addr_flag;
    bmi323_read_data_lsb_idx = data_lsb_idx;

    return (uint8_t)(reg_word & 0x00FFu) == BMI323_CHIP_ID_EXPECTED;
}

static bool bmi323_detect_read_mode(void)
{
    static const uint8_t addr_flags[] = { 0x80u, 0x00u };
    static const uint8_t data_idxs[] = { 1u, 2u, 3u };
    static const uint8_t cpols[] = {
        SPIM_CONFIG_CPOL_ActiveHigh,
        SPIM_CONFIG_CPOL_ActiveLow
    };
    static const uint8_t cphas[] = {
        SPIM_CONFIG_CPHA_Leading,
        SPIM_CONFIG_CPHA_Trailing
    };

    for (uint8_t mode = 0u; mode < (sizeof(cpols) / sizeof(cpols[0])); mode++)
    {
        for (uint8_t phase = 0u; phase < (sizeof(cphas) / sizeof(cphas[0])); phase++)
        {
            bmi323_spi_set_mode(cpols[mode], cphas[phase]);

            for (uint8_t flag = 0u; flag < (sizeof(addr_flags) / sizeof(addr_flags[0])); flag++)
            {
                for (uint8_t idx = 0u; idx < (sizeof(data_idxs) / sizeof(data_idxs[0])); idx++)
                {
                    if (bmi323_try_read_chip_id_mode(addr_flags[flag], data_idxs[idx]))
                    {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

static bool bmi323_read_reg16(uint8_t addr, uint16_t *value)
{
    uint8_t tx[5] = { 0 };
    uint8_t rx[5] = { 0 };

    if (value == NULL)
    {
        return false;
    }

    tx[0] = (uint8_t)(addr | bmi323_read_addr_flag);
    tx[1] = 0xFFu;
    tx[2] = 0xFFu;
    tx[3] = 0xFFu;
    tx[4] = 0xFFu;

    nrf_gpio_pin_clear(BMI323_CS_PIN);
    nrf_delay_us(1u);
    if (!bmi323_spi_xfer(tx, rx, sizeof(tx)))
    {
        nrf_gpio_pin_set(BMI323_CS_PIN);
        nrf_delay_us(2u);
        return false;
    }
    nrf_delay_us(1u);
    nrf_gpio_pin_set(BMI323_CS_PIN);
    nrf_delay_us(2u);

    if ((bmi323_read_data_lsb_idx + 1u) >= sizeof(rx))
    {
        return false;
    }

    *value = (uint16_t)rx[bmi323_read_data_lsb_idx] |
             ((uint16_t)rx[bmi323_read_data_lsb_idx + 1u] << 8);
    return true;
}

static bool bmi323_write_reg16(uint8_t addr, uint16_t value)
{
    uint8_t tx[3];

    tx[0] = (uint8_t)(addr & 0x7Fu);
    tx[1] = (uint8_t)(value & 0x00FFu);
    tx[2] = (uint8_t)((value >> 8) & 0x00FFu);

    nrf_gpio_pin_clear(BMI323_CS_PIN);
    nrf_delay_us(1u);
    if (!bmi323_spi_xfer(tx, NULL, sizeof(tx)))
    {
        nrf_gpio_pin_set(BMI323_CS_PIN);
        nrf_delay_us(2u);
        return false;
    }
    nrf_delay_us(1u);
    nrf_gpio_pin_set(BMI323_CS_PIN);
    nrf_delay_us(2u);
    return true;
}

static int16_t bmi323_raw_to_mg(int16_t raw)
{
    int32_t num = (int32_t)raw * 8000;

    if (num >= 0)
    {
        return (int16_t)((num + 16384) / 32768);
    }
    return (int16_t)((num - 16384) / 32768);
}

static bool ACCEL_MAYBE_UNUSED bmi323_init(void)
{
    uint16_t chip_id_reg;
    uint16_t err_reg;

    bmi323_spi_init();
    nrf_delay_ms(2u);

    if (!bmi323_detect_read_mode())
    {
        bmi323_bitbang_init();
        nrf_delay_ms(2u);

        if (!bmi323_detect_read_mode())
        {
            return false;
        }
    }

    if (!bmi323_read_reg16(BMI323_REG_CHIP_ID, &chip_id_reg))
    {
        return false;
    }
    if ((uint8_t)(chip_id_reg & 0x00FFu) != BMI323_CHIP_ID_EXPECTED)
    {
        return false;
    }
    if (!bmi323_write_reg16(BMI323_REG_ACC_CONF, BMI323_ACC_CONF_VALUE))
    {
        return false;
    }

    nrf_delay_ms(5u);

    if (!bmi323_read_reg16(BMI323_REG_ERR_REG, &err_reg))
    {
        return false;
    }

    return (err_reg & BMI323_ERR_FATAL_MASK) == 0u;
}

static bool bmi323_read(accel_data_t *data)
{
    uint16_t raw_x;
    uint16_t raw_y;
    uint16_t raw_z;

    if (data == NULL)
    {
        return false;
    }
    if (!bmi323_read_reg16(BMI323_REG_ACC_DATA_X, &raw_x))
    {
        return false;
    }
    if (!bmi323_read_reg16((uint8_t)(BMI323_REG_ACC_DATA_X + 1u), &raw_y))
    {
        return false;
    }
    if (!bmi323_read_reg16((uint8_t)(BMI323_REG_ACC_DATA_X + 2u), &raw_z))
    {
        return false;
    }
    if ((raw_x == 0x8000u) || (raw_y == 0x8000u) || (raw_z == 0x8000u))
    {
        return false;
    }

    data->x = bmi323_raw_to_mg((int16_t)raw_x);
    data->y = bmi323_raw_to_mg((int16_t)raw_y);
    data->z = bmi323_raw_to_mg((int16_t)raw_z);

    return true;
}

/* -------------------------------------------------------------------------
 * LIS2DH12 backend, internal DWM3001C I2C bus.
 * ------------------------------------------------------------------------- */

#define LIS_SDA_PIN NRF_GPIO_PIN_MAP(0, 24)
#define LIS_SCL_PIN NRF_GPIO_PIN_MAP(1, 4)

#define LIS2DH12_ADDR_HIGH 0x19u
#define LIS2DH12_ADDR_LOW  0x18u

#define LIS2DH12_WHO_AM_I     0x0Fu
#define LIS2DH12_WHO_AM_I_VAL 0x33u
#define LIS2DH12_CTRL_REG1    0x20u
#define LIS2DH12_CTRL_REG4    0x23u
#define LIS2DH12_OUT_X_L      0x28u

#define LIS_TWI NRF_TWIM1
#define LIS_TWI_TIMEOUT 640000u

static void lis2dh12_twim_init(void)
{
    NRF_UARTE1->ENABLE = 0;
    NRF_SPIM1->ENABLE = 0;
    NRF_SPI1->ENABLE = 0;

    nrf_gpio_cfg(LIS_SCL_PIN,
        NRF_GPIO_PIN_DIR_INPUT,
        NRF_GPIO_PIN_INPUT_CONNECT,
        NRF_GPIO_PIN_PULLUP,
        NRF_GPIO_PIN_S0D1,
        NRF_GPIO_PIN_NOSENSE);

    nrf_gpio_cfg(LIS_SDA_PIN,
        NRF_GPIO_PIN_DIR_INPUT,
        NRF_GPIO_PIN_INPUT_CONNECT,
        NRF_GPIO_PIN_PULLUP,
        NRF_GPIO_PIN_S0D1,
        NRF_GPIO_PIN_NOSENSE);

    LIS_TWI->PSEL.SCL = LIS_SCL_PIN;
    LIS_TWI->PSEL.SDA = LIS_SDA_PIN;
    LIS_TWI->ADDRESS = LIS2DH12_ADDR_HIGH;
    LIS_TWI->FREQUENCY = TWIM_FREQUENCY_FREQUENCY_K100;
    LIS_TWI->ENABLE = TWIM_ENABLE_ENABLE_Enabled;
}

static bool lis2dh12_twi_write(const uint8_t *data, uint8_t len)
{
    volatile uint32_t timeout = LIS_TWI_TIMEOUT;

    LIS_TWI->TXD.PTR = (uint32_t)data;
    LIS_TWI->TXD.MAXCNT = len;
    LIS_TWI->EVENTS_STOPPED = 0;
    LIS_TWI->EVENTS_ERROR = 0;
    LIS_TWI->SHORTS = TWIM_SHORTS_LASTTX_STOP_Msk;
    LIS_TWI->TASKS_STARTTX = 1;

    while (!LIS_TWI->EVENTS_STOPPED && !LIS_TWI->EVENTS_ERROR && (--timeout > 0u)) { }

    if ((timeout == 0u) || LIS_TWI->EVENTS_ERROR)
    {
        LIS_TWI->EVENTS_ERROR = 0;
        LIS_TWI->TASKS_STOP = 1;
        timeout = LIS_TWI_TIMEOUT;
        while (!LIS_TWI->EVENTS_STOPPED && (--timeout > 0u)) { }
        LIS_TWI->EVENTS_STOPPED = 0;
        return false;
    }

    LIS_TWI->EVENTS_STOPPED = 0;
    return true;
}

static bool lis2dh12_twi_write_then_read(const uint8_t *tx, uint8_t tx_len,
                                         uint8_t *rx, uint8_t rx_len)
{
    volatile uint32_t timeout = LIS_TWI_TIMEOUT;

    LIS_TWI->TXD.PTR = (uint32_t)tx;
    LIS_TWI->TXD.MAXCNT = tx_len;
    LIS_TWI->RXD.PTR = (uint32_t)rx;
    LIS_TWI->RXD.MAXCNT = rx_len;
    LIS_TWI->EVENTS_STOPPED = 0;
    LIS_TWI->EVENTS_ERROR = 0;
    LIS_TWI->SHORTS = TWIM_SHORTS_LASTTX_STARTRX_Msk | TWIM_SHORTS_LASTRX_STOP_Msk;
    LIS_TWI->TASKS_STARTTX = 1;

    while (!LIS_TWI->EVENTS_STOPPED && !LIS_TWI->EVENTS_ERROR && (--timeout > 0u)) { }

    if ((timeout == 0u) || LIS_TWI->EVENTS_ERROR)
    {
        LIS_TWI->EVENTS_ERROR = 0;
        LIS_TWI->TASKS_STOP = 1;
        timeout = LIS_TWI_TIMEOUT;
        while (!LIS_TWI->EVENTS_STOPPED && (--timeout > 0u)) { }
        LIS_TWI->EVENTS_STOPPED = 0;
        return false;
    }

    LIS_TWI->EVENTS_STOPPED = 0;
    return true;
}

static bool lis2dh12_write_reg(uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    return lis2dh12_twi_write(buf, 2);
}

static bool lis2dh12_read_reg(uint8_t reg, uint8_t *val)
{
    return lis2dh12_twi_write_then_read(&reg, 1, val, 1);
}

static bool ACCEL_MAYBE_UNUSED lis2dh12_init(void)
{
    uint8_t who = 0u;

    lis2dh12_twim_init();

    if (!lis2dh12_read_reg(LIS2DH12_WHO_AM_I, &who) || (who != LIS2DH12_WHO_AM_I_VAL))
    {
        LIS_TWI->ENABLE = 0;
        LIS_TWI->ADDRESS = LIS2DH12_ADDR_LOW;
        LIS_TWI->ENABLE = TWIM_ENABLE_ENABLE_Enabled;
        who = 0u;
        if (!lis2dh12_read_reg(LIS2DH12_WHO_AM_I, &who) || (who != LIS2DH12_WHO_AM_I_VAL))
        {
            return false;
        }
    }

    if (!lis2dh12_write_reg(LIS2DH12_CTRL_REG1, 0x57u))
    {
        return false;
    }
    if (!lis2dh12_write_reg(LIS2DH12_CTRL_REG4, 0x08u))
    {
        return false;
    }

    return true;
}

static bool lis2dh12_read(accel_data_t *data)
{
    uint8_t raw[6];
    uint8_t reg = LIS2DH12_OUT_X_L | 0x80u;
    int16_t raw_x;
    int16_t raw_y;
    int16_t raw_z;

    if (data == NULL)
    {
        return false;
    }
    if (!lis2dh12_twi_write_then_read(&reg, 1, raw, 6))
    {
        return false;
    }

    raw_x = (int16_t)(raw[1] << 8 | raw[0]) >> 4;
    raw_y = (int16_t)(raw[3] << 8 | raw[2]) >> 4;
    raw_z = (int16_t)(raw[5] << 8 | raw[4]) >> 4;

    data->x = raw_x;
    data->y = raw_y;
    data->z = raw_z;

    return true;
}

bool accel_init(void)
{
    active_backend = ACCEL_BACKEND_NONE;

#if defined(UWB_ACCEL_TARGET_GENA)
    if (bmi323_init())
    {
        active_backend = ACCEL_BACKEND_BMI323;
        return true;
    }
#elif defined(UWB_ACCEL_TARGET_LEGACY)
    if (lis2dh12_init())
    {
        active_backend = ACCEL_BACKEND_LIS2DH12;
        return true;
    }
#else
    if (bmi323_init())
    {
        active_backend = ACCEL_BACKEND_BMI323;
        return true;
    }
    if (lis2dh12_init())
    {
        active_backend = ACCEL_BACKEND_LIS2DH12;
        return true;
    }
#endif

    return false;
}

bool accel_read(accel_data_t *data)
{
    if (active_backend == ACCEL_BACKEND_BMI323)
    {
        return bmi323_read(data);
    }
    if (active_backend == ACCEL_BACKEND_LIS2DH12)
    {
        return lis2dh12_read(data);
    }

    return false;
}

const char *accel_backend_name(void)
{
    if (active_backend == ACCEL_BACKEND_BMI323)
    {
        return "ACCEL OK (BMI323)";
    }
    if (active_backend == ACCEL_BACKEND_LIS2DH12)
    {
        return "ACCEL OK (LIS2DH12)";
    }

    return "ACCEL NONE";
}

const char *accel_backend_label(void)
{
    if (active_backend == ACCEL_BACKEND_BMI323)
    {
        return "BMI323";
    }
    if (active_backend == ACCEL_BACKEND_LIS2DH12)
    {
        return "LIS2DH12";
    }

    return "NONE";
}

uint8_t accel_backend_code(void)
{
    if (active_backend == ACCEL_BACKEND_BMI323)
    {
        return ACCEL_BACKEND_CODE_BMI323;
    }
    if (active_backend == ACCEL_BACKEND_LIS2DH12)
    {
        return ACCEL_BACKEND_CODE_LIS2DH12;
    }

    return ACCEL_BACKEND_CODE_NONE;
}

bool accel_backend_is_bmi323(void)
{
    return active_backend == ACCEL_BACKEND_BMI323;
}
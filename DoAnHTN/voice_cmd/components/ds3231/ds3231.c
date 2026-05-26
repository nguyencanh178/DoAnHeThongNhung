#include "ds3231.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "DS3231";

static i2c_port_t s_port = I2C_NUM_0;
static bool s_initialized = false;

/* DS3231 registers */
#define DS3231_REG_TIME_START   0x00
#define DS3231_REG_CONTROL      0x0E
#define DS3231_REG_STATUS       0x0F
#define DS3231_REG_TEMP_MSB     0x11

static uint8_t dec_to_bcd(uint8_t val)
{
    return (uint8_t)(((val / 10) << 4) | (val % 10));
}

static uint8_t bcd_to_dec(uint8_t val)
{
    return (uint8_t)(((val >> 4) * 10) + (val & 0x0F));
}

static esp_err_t ds3231_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2] = {reg, value};
    return i2c_master_write_to_device(
        s_port,
        DS3231_I2C_ADDRESS,
        data,
        sizeof(data),
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t ds3231_write_regs(uint8_t start_reg, const uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t buffer[8];
    if ((len + 1) > sizeof(buffer)) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = start_reg;
    memcpy(&buffer[1], data, len);

    return i2c_master_write_to_device(
        s_port,
        DS3231_I2C_ADDRESS,
        buffer,
        len + 1,
        pdMS_TO_TICKS(100)
    );
}

static esp_err_t ds3231_read_regs(uint8_t start_reg, uint8_t *data, size_t len)
{
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_write_read_device(
        s_port,
        DS3231_I2C_ADDRESS,
        &start_reg,
        1,
        data,
        len,
        pdMS_TO_TICKS(100)
    );
}

static bool ds3231_time_valid(const ds3231_time_t *time)
{
    if (time == NULL) return false;
    if (time->second > 59) return false;
    if (time->minute > 59) return false;
    if (time->hour > 23) return false;
    if (time->day < 1 || time->day > 7) return false;
    if (time->date < 1 || time->date > 31) return false;
    if (time->month < 1 || time->month > 12) return false;
    if (time->year < 2000 || time->year > 2099) return false;
    return true;
}

esp_err_t ds3231_init(const ds3231_config_t *config)
{
    esp_err_t ret;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_port = config->port;

    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = config->sda_pin,
        .scl_io_num = config->scl_pin,
        .sda_pullup_en = config->pullup_en ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .scl_pullup_en = config->pullup_en ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .master.clk_speed = config->clk_speed_hz,
        .clk_flags = 0
    };

    ret = i2c_param_config(s_port, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed");
        return ret;
    }

    ret = i2c_driver_install(s_port, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGW(TAG, "i2c driver already installed");
            s_initialized = true;
            return ESP_OK;
        }
        ESP_LOGE(TAG, "i2c_driver_install failed");
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "DS3231 init done on I2C port %d", s_port);
    return ESP_OK;
}

esp_err_t ds3231_set_time(const ds3231_time_t *time)
{
    uint8_t data[7];

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!ds3231_time_valid(time)) {
        return ESP_ERR_INVALID_ARG;
    }

    data[0] = dec_to_bcd(time->second);
    data[1] = dec_to_bcd(time->minute);
    data[2] = dec_to_bcd(time->hour);   // 24h mode
    data[3] = dec_to_bcd(time->day);
    data[4] = dec_to_bcd(time->date);
    data[5] = dec_to_bcd(time->month);
    data[6] = dec_to_bcd((uint8_t)(time->year % 100));

    return ds3231_write_regs(DS3231_REG_TIME_START, data, sizeof(data));
}

esp_err_t ds3231_get_time(ds3231_time_t *time)
{
    esp_err_t ret;
    uint8_t data[7];

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (time == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ds3231_read_regs(DS3231_REG_TIME_START, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read time failed");
        return ret;
    }

    time->second = bcd_to_dec(data[0] & 0x7F);
    time->minute = bcd_to_dec(data[1] & 0x7F);
    time->hour   = bcd_to_dec(data[2] & 0x3F);
    time->day    = bcd_to_dec(data[3] & 0x07);
    time->date   = bcd_to_dec(data[4] & 0x3F);
    time->month  = bcd_to_dec(data[5] & 0x1F);
    time->year   = 2000 + bcd_to_dec(data[6]);

    return ESP_OK;
}

esp_err_t ds3231_get_temperature(float *temperature)
{
    esp_err_t ret;
    uint8_t data[2];
    int8_t msb;
    uint8_t frac;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ds3231_read_regs(DS3231_REG_TEMP_MSB, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read temperature failed");
        return ret;
    }

    msb = (int8_t)data[0];
    frac = (data[1] >> 6) & 0x03;

    *temperature = (float)msb + ((float)frac * 0.25f);
    return ESP_OK;
}

esp_err_t ds3231_get_oscillator_stop_flag(bool *stopped)
{
    esp_err_t ret;
    uint8_t status = 0;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (stopped == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ds3231_read_regs(DS3231_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read status failed");
        return ret;
    }

    *stopped = (status & (1 << 7)) ? true : false;
    return ESP_OK;
}

esp_err_t ds3231_clear_oscillator_stop_flag(void)
{
    esp_err_t ret;
    uint8_t status = 0;

    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ret = ds3231_read_regs(DS3231_REG_STATUS, &status, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "read status failed");
        return ret;
    }

    status &= ~(1 << 7);
    return ds3231_write_reg(DS3231_REG_STATUS, status);
}
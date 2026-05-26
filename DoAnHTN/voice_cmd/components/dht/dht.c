#include "dht.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/portmacro.h"
#include <string.h>

static const char *TAG = "DHT";

static dht_config_t s_dht_cfg;
static bool s_dht_inited = false;
static portMUX_TYPE dht_mux = portMUX_INITIALIZER_UNLOCKED;

#define DHT_RESPONSE_TIMEOUT_US   200
#define DHT_BIT_LOW_TIMEOUT_US    100
#define DHT_BIT_HIGH_TIMEOUT_US   120

static esp_err_t dht_set_pin_output(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_dht_cfg.pin),
        .mode = GPIO_MODE_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    return gpio_config(&io_conf);
}

static esp_err_t dht_set_pin_input(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << s_dht_cfg.pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    return gpio_config(&io_conf);
}

static int dht_wait_for_level(int level, uint32_t timeout_us)
{
    uint32_t count = 0;

    while (gpio_get_level(s_dht_cfg.pin) == level) {
        esp_rom_delay_us(1);
        count++;
        if (count >= timeout_us) {
            return -1;
        }
    }

    return (int)count;
}

esp_err_t dht_init(const dht_config_t *config)
{
    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    s_dht_cfg = *config;
    s_dht_inited = true;

    if (dht_set_pin_input() != ESP_OK) {
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "DHT init done on GPIO %d, type=%s",
             s_dht_cfg.pin,
             (s_dht_cfg.type == DHT_TYPE_DHT22) ? "DHT22" : "DHT11");

    return ESP_OK;
}

esp_err_t dht_read(dht_data_t *data)
{
    uint8_t raw[5] = {0};

    if (!s_dht_inited || data == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(data, 0, sizeof(dht_data_t));

    if (dht_set_pin_output() != ESP_OK) {
        return ESP_FAIL;
    }

    gpio_set_level(s_dht_cfg.pin, 0);

    if (s_dht_cfg.type == DHT_TYPE_DHT22) {
        esp_rom_delay_us(1500);
    } else {
        esp_rom_delay_us(18000);
    }

    gpio_set_level(s_dht_cfg.pin, 1);
    esp_rom_delay_us(30);

    if (dht_set_pin_input() != ESP_OK) {
        return ESP_FAIL;
    }

    if (dht_wait_for_level(1, DHT_RESPONSE_TIMEOUT_US) < 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht_wait_for_level(0, DHT_RESPONSE_TIMEOUT_US) < 0) {
        return ESP_ERR_TIMEOUT;
    }
    if (dht_wait_for_level(1, DHT_RESPONSE_TIMEOUT_US) < 0) {
        return ESP_ERR_TIMEOUT;
    }

    portENTER_CRITICAL(&dht_mux);

    for (int i = 0; i < 40; i++) {
        if (dht_wait_for_level(0, DHT_BIT_LOW_TIMEOUT_US) < 0) {
            portEXIT_CRITICAL(&dht_mux);
            return ESP_ERR_TIMEOUT;
        }

        int high_time = dht_wait_for_level(1, DHT_BIT_HIGH_TIMEOUT_US);
        if (high_time < 0) {
            portEXIT_CRITICAL(&dht_mux);
            return ESP_ERR_TIMEOUT;
        }

        raw[i / 8] <<= 1;
        if (high_time > 40) {
            raw[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL(&dht_mux);

    uint8_t checksum = (uint8_t)(raw[0] + raw[1] + raw[2] + raw[3]);
    if (checksum != raw[4]) {
        ESP_LOGE(TAG, "Checksum error: calc=%u recv=%u", checksum, raw[4]);
        return ESP_ERR_INVALID_CRC;
    }

    if (s_dht_cfg.type == DHT_TYPE_DHT11) {
        data->humidity = raw[0];
        data->temperature = raw[2];
    } else {
        uint16_t raw_h = ((uint16_t)raw[0] << 8) | raw[1];
        uint16_t raw_t = ((uint16_t)raw[2] << 8) | raw[3];

        data->humidity = raw_h / 10.0f;

        if (raw_t & 0x8000) {
            raw_t &= 0x7FFF;
            data->temperature = -((float)raw_t / 10.0f);
        } else {
            data->temperature = raw_t / 10.0f;
        }
    }

    return ESP_OK;
}

esp_err_t dht_read_temperature(float *temperature)
{
    dht_data_t data;
    esp_err_t ret;

    if (temperature == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = dht_read(&data);
    if (ret != ESP_OK) {
        return ret;
    }

    *temperature = data.temperature;
    return ESP_OK;
}

esp_err_t dht_read_humidity(float *humidity)
{
    dht_data_t data;
    esp_err_t ret;

    if (humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = dht_read(&data);
    if (ret != ESP_OK) {
        return ret;
    }

    *humidity = data.humidity;
    return ESP_OK;
}
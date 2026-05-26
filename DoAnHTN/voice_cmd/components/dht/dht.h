#ifndef DHT_H
#define DHT_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DHT_TYPE_DHT11 = 0,
    DHT_TYPE_DHT22 = 1
} dht_type_t;

typedef struct {
    gpio_num_t pin;
    dht_type_t type;
} dht_config_t;

typedef struct {
    float temperature;
    float humidity;
} dht_data_t;

/**
 * @brief Khởi tạo cấu hình DHT
 */
esp_err_t dht_init(const dht_config_t *config);

/**
 * @brief Đọc dữ liệu nhiệt độ/độ ẩm
 */
esp_err_t dht_read(dht_data_t *data);

/**
 * @brief Đọc riêng nhiệt độ
 */
esp_err_t dht_read_temperature(float *temperature);

/**
 * @brief Đọc riêng độ ẩm
 */
esp_err_t dht_read_humidity(float *humidity);

#ifdef __cplusplus
}
#endif

#endif
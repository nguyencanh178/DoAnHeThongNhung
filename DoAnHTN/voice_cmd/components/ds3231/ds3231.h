#ifndef DS3231_H
#define DS3231_H

#include <stdbool.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DS3231_I2C_ADDRESS 0x68

typedef struct {
    uint8_t second;   // 0..59
    uint8_t minute;   // 0..59
    uint8_t hour;     // 0..23
    uint8_t day;      // 1..7
    uint8_t date;     // 1..31
    uint8_t month;    // 1..12
    uint16_t year;    // 2000..2099
} ds3231_time_t;

typedef struct {
    i2c_port_t port;
    gpio_num_t sda_pin;
    gpio_num_t scl_pin;
    uint32_t clk_speed_hz;
    bool pullup_en;
} ds3231_config_t;

esp_err_t ds3231_init(const ds3231_config_t *config);
esp_err_t ds3231_set_time(const ds3231_time_t *time);
esp_err_t ds3231_get_time(ds3231_time_t *time);
esp_err_t ds3231_get_temperature(float *temperature);

esp_err_t ds3231_get_oscillator_stop_flag(bool *stopped);
esp_err_t ds3231_clear_oscillator_stop_flag(void);

#ifdef __cplusplus
}
#endif

#endif
#ifndef ILI9225_H
#define ILI9225_H

#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ILI9225_WIDTH   176
#define ILI9225_HEIGHT  220

typedef struct
{
    spi_host_device_t spi_host;

    gpio_num_t mosi;
    gpio_num_t sclk;
    gpio_num_t cs;
    gpio_num_t dc;
    gpio_num_t rst;

    int clock_speed_hz;

} ili9225_config_t;

esp_err_t ili9225_init(const ili9225_config_t *cfg);

void ili9225_fill_screen(uint16_t color);

void ili9225_draw_pixel(uint16_t x, uint16_t y, uint16_t color);

void ili9225_draw_rect(uint16_t x, uint16_t y,
                       uint16_t w, uint16_t h,
                       uint16_t color);

void ili9225_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg);
void ili9225_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg);

#ifdef __cplusplus
}
#endif

#endif
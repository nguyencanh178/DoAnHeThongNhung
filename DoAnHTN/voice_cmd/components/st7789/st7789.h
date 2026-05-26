#ifndef ST7789_H
#define ST7789_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    spi_host_device_t spi_host;

    gpio_num_t pin_mosi;
    gpio_num_t pin_sclk;
    gpio_num_t pin_cs;
    gpio_num_t pin_dc;
    gpio_num_t pin_rst;

    int pin_miso;      // = -1 neu khong dung
    int pin_bl;        // = -1 neu module khong co BLK

    uint16_t width;    // logical width
    uint16_t height;   // logical height

    uint16_t x_offset; // offset phan cung cua panel
    uint16_t y_offset;

    uint32_t clk_hz;
    bool swap_xy;
    bool mirror_x;
    bool mirror_y;
    bool rgb_order;    // false=BGR, true=RGB
    uint8_t rotation;  // 0..3
} st7789_config_t;

typedef struct
{
    spi_device_handle_t spi;
    st7789_config_t cfg;
    uint16_t width;
    uint16_t height;
} st7789_t;

/* Mau 16-bit RGB565 */
#define ST7789_COLOR_BLACK   0x0000
#define ST7789_COLOR_WHITE   0xFFFF
#define ST7789_COLOR_RED     0xF800
#define ST7789_COLOR_GREEN   0x07E0
#define ST7789_COLOR_BLUE    0x001F
#define ST7789_COLOR_YELLOW  0xFFE0
#define ST7789_COLOR_CYAN    0x07FF
#define ST7789_COLOR_MAGENTA 0xF81F

esp_err_t st7789_init(st7789_t *lcd, const st7789_config_t *cfg);
esp_err_t st7789_set_rotation(st7789_t *lcd, uint8_t rotation);

esp_err_t st7789_draw_pixel(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t color);
esp_err_t st7789_fill_screen(st7789_t *lcd, uint16_t color);
esp_err_t st7789_fill_rect(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
esp_err_t st7789_draw_fast_hline(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t color);
esp_err_t st7789_draw_fast_vline(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t h, uint16_t color);
esp_err_t st7789_draw_char(st7789_t *lcd, uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
esp_err_t st7789_draw_string(st7789_t *lcd, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);

uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b);

#ifdef __cplusplus
}
#endif

#endif
#include "st7789.h"
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

#define TAG "ST7789"

#define ST7789_CMD_SWRESET  0x01
#define ST7789_CMD_SLPIN    0x10
#define ST7789_CMD_SLPOUT   0x11
#define ST7789_CMD_NORON    0x13
#define ST7789_CMD_INVOFF   0x20
#define ST7789_CMD_INVON    0x21
#define ST7789_CMD_DISPOFF  0x28
#define ST7789_CMD_DISPON   0x29
#define ST7789_CMD_CASET    0x2A
#define ST7789_CMD_RASET    0x2B
#define ST7789_CMD_RAMWR    0x2C
#define ST7789_CMD_MADCTL   0x36
#define ST7789_CMD_COLMOD   0x3A

#define MADCTL_MY  0x80
#define MADCTL_MX  0x40
#define MADCTL_MV  0x20
#define MADCTL_RGB 0x00
#define MADCTL_BGR 0x08

static esp_err_t st7789_send_cmd(st7789_t *lcd, const uint8_t cmd)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    gpio_set_level(lcd->cfg.pin_dc, 0);

    t.length = 8;
    t.tx_buffer = &cmd;

    return spi_device_transmit(lcd->spi, &t);
}

static esp_err_t st7789_send_data(st7789_t *lcd, const void *data, int len)
{
    if (len <= 0) return ESP_OK;

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));

    gpio_set_level(lcd->cfg.pin_dc, 1);

    t.length = len * 8;
    t.tx_buffer = data;

    return spi_device_transmit(lcd->spi, &t);
}

static esp_err_t st7789_reset(st7789_t *lcd)
{
    if (lcd->cfg.pin_rst < 0) return ESP_OK;

    gpio_set_level(lcd->cfg.pin_rst, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(lcd->cfg.pin_rst, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    return ESP_OK;
}

static esp_err_t st7789_set_addr_window(st7789_t *lcd, uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    esp_err_t ret;
    uint8_t data[4];

    x0 += lcd->cfg.x_offset;
    x1 += lcd->cfg.x_offset;
    y0 += lcd->cfg.y_offset;
    y1 += lcd->cfg.y_offset;

    ret = st7789_send_cmd(lcd, ST7789_CMD_CASET);
    if (ret != ESP_OK) return ret;
    data[0] = x0 >> 8;
    data[1] = x0 & 0xFF;
    data[2] = x1 >> 8;
    data[3] = x1 & 0xFF;
    ret = st7789_send_data(lcd, data, 4);
    if (ret != ESP_OK) return ret;

    ret = st7789_send_cmd(lcd, ST7789_CMD_RASET);
    if (ret != ESP_OK) return ret;
    data[0] = y0 >> 8;
    data[1] = y0 & 0xFF;
    data[2] = y1 >> 8;
    data[3] = y1 & 0xFF;
    ret = st7789_send_data(lcd, data, 4);
    if (ret != ESP_OK) return ret;

    ret = st7789_send_cmd(lcd, ST7789_CMD_RAMWR);
    return ret;
}

uint16_t st7789_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

esp_err_t st7789_set_rotation(st7789_t *lcd, uint8_t rotation)
{
    esp_err_t ret;
    uint8_t madctl = 0;
    rotation &= 3;
    lcd->cfg.rotation = rotation;

    switch (rotation)
    {
        case 0:
            madctl = lcd->cfg.rgb_order ? MADCTL_RGB : MADCTL_BGR;
            if (lcd->cfg.mirror_x) madctl |= MADCTL_MX;
            if (lcd->cfg.mirror_y) madctl |= MADCTL_MY;
            lcd->width  = lcd->cfg.width;
            lcd->height = lcd->cfg.height;
            break;

        case 1:
            madctl = MADCTL_MV | MADCTL_MX | (lcd->cfg.rgb_order ? MADCTL_RGB : MADCTL_BGR);
            lcd->width  = lcd->cfg.height;
            lcd->height = lcd->cfg.width;
            break;

        case 2:
            madctl = MADCTL_MX | MADCTL_MY | (lcd->cfg.rgb_order ? MADCTL_RGB : MADCTL_BGR);
            lcd->width  = lcd->cfg.width;
            lcd->height = lcd->cfg.height;
            break;

        case 3:
            madctl = MADCTL_MV | MADCTL_MY | (lcd->cfg.rgb_order ? MADCTL_RGB : MADCTL_BGR);
            lcd->width  = lcd->cfg.height;
            lcd->height = lcd->cfg.width;
            break;
    }

    ret = st7789_send_cmd(lcd, ST7789_CMD_MADCTL);
    if (ret != ESP_OK) return ret;
    return st7789_send_data(lcd, &madctl, 1);
}

esp_err_t st7789_init(st7789_t *lcd, const st7789_config_t *cfg)
{
    if (!lcd || !cfg) return ESP_ERR_INVALID_ARG;
    memset(lcd, 0, sizeof(*lcd));
    lcd->cfg = *cfg;
    lcd->width = cfg->width;
    lcd->height = cfg->height;

    esp_err_t ret;

    spi_bus_config_t buscfg = {
        .mosi_io_num = cfg->pin_mosi,
        .miso_io_num = cfg->pin_miso,
        .sclk_io_num = cfg->pin_sclk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = cfg->width * 40 * 2 + 8
    };

    ret = spi_bus_initialize(cfg->spi_host, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = cfg->clk_hz,
        .mode = 0,
        .spics_io_num = cfg->pin_cs,
        .queue_size = 7,
        .flags = SPI_DEVICE_NO_DUMMY
    };

    ret = spi_bus_add_device(cfg->spi_host, &devcfg, &lcd->spi);
    if (ret != ESP_OK) return ret;

    gpio_config_t io_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << cfg->pin_dc) |
                        ((cfg->pin_rst >= 0) ? (1ULL << cfg->pin_rst) : 0) |
                        ((cfg->pin_bl  >= 0) ? (1ULL << cfg->pin_bl)  : 0),
        .pull_down_en = 0,
        .pull_up_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ret = gpio_config(&io_conf);
    if (ret != ESP_OK) return ret;

    if (cfg->pin_bl >= 0) {
        gpio_set_level(cfg->pin_bl, 1);
    }

    ret = st7789_reset(lcd);
    if (ret != ESP_OK) return ret;

    ret = st7789_send_cmd(lcd, ST7789_CMD_SWRESET);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(150));

    ret = st7789_send_cmd(lcd, ST7789_CMD_SLPOUT);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(120));

    {
        uint8_t colmod = 0x55; // 16-bit/pixel
        ret = st7789_send_cmd(lcd, ST7789_CMD_COLMOD);
        if (ret != ESP_OK) return ret;
        ret = st7789_send_data(lcd, &colmod, 1);
        if (ret != ESP_OK) return ret;
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    ret = st7789_set_rotation(lcd, cfg->rotation);
    if (ret != ESP_OK) return ret;

    ret = st7789_send_cmd(lcd, ST7789_CMD_INVON);   // nhieu module ST7789 can inversion on
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = st7789_send_cmd(lcd, ST7789_CMD_NORON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(10));

    ret = st7789_send_cmd(lcd, ST7789_CMD_DISPON);
    if (ret != ESP_OK) return ret;
    vTaskDelay(pdMS_TO_TICKS(100));

    ESP_LOGI(TAG, "ST7789 init done, w=%u h=%u", lcd->width, lcd->height);
    return ESP_OK;
}

esp_err_t st7789_draw_pixel(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t color)
{
    if (!lcd) return ESP_ERR_INVALID_ARG;
    if (x >= lcd->width || y >= lcd->height) return ESP_OK;

    esp_err_t ret = st7789_set_addr_window(lcd, x, y, x, y);
    if (ret != ESP_OK) return ret;

    uint8_t data[2] = { color >> 8, color & 0xFF };
    return st7789_send_data(lcd, data, 2);
}

esp_err_t st7789_fill_rect(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!lcd) return ESP_ERR_INVALID_ARG;
    if (x >= lcd->width || y >= lcd->height) return ESP_OK;
    if (w == 0 || h == 0) return ESP_OK;

    if (x + w > lcd->width)  w = lcd->width - x;
    if (y + h > lcd->height) h = lcd->height - y;

    esp_err_t ret = st7789_set_addr_window(lcd, x, y, x + w - 1, y + h - 1);
    if (ret != ESP_OK) return ret;

    const size_t pixels = (size_t)w * h;
    const size_t chunk_pixels = 512;
    uint8_t *buf = (uint8_t *)malloc(chunk_pixels * 2);
    if (!buf) return ESP_ERR_NO_MEM;

    for (size_t i = 0; i < chunk_pixels; i++) {
        buf[2 * i]     = color >> 8;
        buf[2 * i + 1] = color & 0xFF;
    }

    size_t remain = pixels;
    while (remain > 0) {
        size_t now = (remain > chunk_pixels) ? chunk_pixels : remain;
        ret = st7789_send_data(lcd, buf, now * 2);
        if (ret != ESP_OK) {
            free(buf);
            return ret;
        }
        remain -= now;
    }

    free(buf);
    return ESP_OK;
}

esp_err_t st7789_fill_screen(st7789_t *lcd, uint16_t color)
{
    if (!lcd) return ESP_ERR_INVALID_ARG;
    return st7789_fill_rect(lcd, 0, 0, lcd->width, lcd->height, color);
}

esp_err_t st7789_draw_fast_hline(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    return st7789_fill_rect(lcd, x, y, w, 1, color);
}

esp_err_t st7789_draw_fast_vline(st7789_t *lcd, uint16_t x, uint16_t y, uint16_t h, uint16_t color)
{
    return st7789_fill_rect(lcd, x, y, 1, h, color);
}

static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, // space 32
    {0x00,0x00,0x5F,0x00,0x00}, // !
    {0x00,0x07,0x00,0x07,0x00}, // "
    {0x14,0x7F,0x14,0x7F,0x14}, // #
    {0x24,0x2A,0x7F,0x2A,0x12}, // $
    {0x23,0x13,0x08,0x64,0x62}, // %
    {0x36,0x49,0x55,0x22,0x50}, // &
    {0x00,0x05,0x03,0x00,0x00}, // '
    {0x00,0x1C,0x22,0x41,0x00}, // (
    {0x00,0x41,0x22,0x1C,0x00}, // )
    {0x14,0x08,0x3E,0x08,0x14}, // *
    {0x08,0x08,0x3E,0x08,0x08}, // +
    {0x00,0x50,0x30,0x00,0x00}, // ,
    {0x08,0x08,0x08,0x08,0x08}, // -
    {0x00,0x60,0x60,0x00,0x00}, // .
    {0x20,0x10,0x08,0x04,0x02}, // /
    {0x3E,0x51,0x49,0x45,0x3E}, // 0
    {0x00,0x42,0x7F,0x40,0x00}, // 1
    {0x42,0x61,0x51,0x49,0x46}, // 2
    {0x21,0x41,0x45,0x4B,0x31}, // 3
    {0x18,0x14,0x12,0x7F,0x10}, // 4
    {0x27,0x45,0x45,0x45,0x39}, // 5
    {0x3C,0x4A,0x49,0x49,0x30}, // 6
    {0x01,0x71,0x09,0x05,0x03}, // 7
    {0x36,0x49,0x49,0x49,0x36}, // 8
    {0x06,0x49,0x49,0x29,0x1E}, // 9
    {0x00,0x36,0x36,0x00,0x00}, // :
    {0x00,0x56,0x36,0x00,0x00}, // ;
    {0x08,0x14,0x22,0x41,0x00}, // <
    {0x14,0x14,0x14,0x14,0x14}, // =
    {0x00,0x41,0x22,0x14,0x08}, // >
    {0x02,0x01,0x51,0x09,0x06}, // ?
    {0x32,0x49,0x79,0x41,0x3E}, // @
    {0x7E,0x11,0x11,0x11,0x7E}, // A
    {0x7F,0x49,0x49,0x49,0x36}, // B
    {0x3E,0x41,0x41,0x41,0x22}, // C
    {0x7F,0x41,0x41,0x22,0x1C}, // D
    {0x7F,0x49,0x49,0x49,0x41}, // E
    {0x7F,0x09,0x09,0x09,0x01}, // F
    {0x3E,0x41,0x49,0x49,0x7A}, // G
    {0x7F,0x08,0x08,0x08,0x7F}, // H
    {0x00,0x41,0x7F,0x41,0x00}, // I
    {0x20,0x40,0x41,0x3F,0x01}, // J
    {0x7F,0x08,0x14,0x22,0x41}, // K
    {0x7F,0x40,0x40,0x40,0x40}, // L
    {0x7F,0x02,0x0C,0x02,0x7F}, // M
    {0x7F,0x04,0x08,0x10,0x7F}, // N
    {0x3E,0x41,0x41,0x41,0x3E}, // O
    {0x7F,0x09,0x09,0x09,0x06}, // P
    {0x3E,0x41,0x51,0x21,0x5E}, // Q
    {0x7F,0x09,0x19,0x29,0x46}, // R
    {0x46,0x49,0x49,0x49,0x31}, // S
    {0x01,0x01,0x7F,0x01,0x01}, // T
    {0x3F,0x40,0x40,0x40,0x3F}, // U
    {0x1F,0x20,0x40,0x20,0x1F}, // V
    {0x7F,0x20,0x18,0x20,0x7F}, // W
    {0x63,0x14,0x08,0x14,0x63}, // X
    {0x07,0x08,0x70,0x08,0x07}, // Y
    {0x61,0x51,0x49,0x45,0x43}, // Z
};

esp_err_t st7789_draw_char(st7789_t *lcd, uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size)
{
    if (!lcd) return ESP_ERR_INVALID_ARG;
    if (c < 32 || c > 90) c = '?';

    const uint8_t *bitmap = font5x7[c - 32];

    for (int i = 0; i < 5; i++) {
        uint8_t line = bitmap[i];
        for (int j = 0; j < 8; j++) {
            uint16_t draw_color = (line & 0x01) ? color : bg;
            if (size == 1) {
                st7789_draw_pixel(lcd, x + i, y + j, draw_color);
            } else {
                st7789_fill_rect(lcd, x + i * size, y + j * size, size, size, draw_color);
            }
            line >>= 1;
        }
    }

    if (size == 1) {
        st7789_fill_rect(lcd, x + 5, y, 1, 8, bg);
    } else {
        st7789_fill_rect(lcd, x + 5 * size, y, size, 8 * size, bg);
    }

    return ESP_OK;
}

esp_err_t st7789_draw_string(st7789_t *lcd, uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size)
{
    if (!lcd || !str) return ESP_ERR_INVALID_ARG;

    while (*str) {
        st7789_draw_char(lcd, x, y, *str, color, bg, size);
        x += 6 * size;
        str++;
    }
    return ESP_OK;
}
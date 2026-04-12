#include "lcd_hat_144.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define LCD_HAT_SPI_MODE SPI_MODE_0
#define LCD_HAT_SPI_BITS_PER_WORD 8
#define LCD_HAT_SPI_SPEED_HZ 16000000U

#define LCD_HAT_GPIO_DC 25
#define LCD_HAT_GPIO_RST 27
#define LCD_HAT_GPIO_BL 24

#define LCD_HAT_BUTTON_COUNT 8

static const unsigned int LCD_HAT_BUTTON_LINES[LCD_HAT_BUTTON_COUNT] = {21, 20, 16, 6, 19, 5, 26, 13};
static const lcd_hat_button_t LCD_HAT_BUTTON_IDS[LCD_HAT_BUTTON_COUNT] = {
    LCD_HAT_BUTTON_KEY1,
    LCD_HAT_BUTTON_KEY2,
    LCD_HAT_BUTTON_KEY3,
    LCD_HAT_BUTTON_UP,
    LCD_HAT_BUTTON_DOWN,
    LCD_HAT_BUTTON_LEFT,
    LCD_HAT_BUTTON_RIGHT,
    LCD_HAT_BUTTON_PRESS
};

static const int LCD_HAT_X_OFFSET = 2;
static const int LCD_HAT_Y_OFFSET = 1;

static int lcd_hat_sleep_ms(long milliseconds) {
    struct timespec duration;
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    return nanosleep(&duration, NULL);
}

static int lcd_hat_request_output_line(int gpiochip_fd, unsigned int offset, int initial_value, const char *consumer) {
    struct gpiohandle_request request;

    memset(&request, 0, sizeof(request));
    request.lines = 1;
    request.lineoffsets[0] = offset;
    request.flags = GPIOHANDLE_REQUEST_OUTPUT;
    request.default_values[0] = initial_value ? 1 : 0;
    strncpy(request.consumer_label, consumer, sizeof(request.consumer_label) - 1);

    if (ioctl(gpiochip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0) {
        fprintf(stderr, "Failed to request GPIO line %u for %s: %s\n", offset, consumer, strerror(errno));
        return -1;
    }

    return request.fd;
}

static int lcd_hat_request_buttons(int gpiochip_fd) {
    struct gpiohandle_request request;

    memset(&request, 0, sizeof(request));
    request.lines = LCD_HAT_BUTTON_COUNT;
    for (int i = 0; i < LCD_HAT_BUTTON_COUNT; i++) {
        request.lineoffsets[i] = LCD_HAT_BUTTON_LINES[i];
    }

    request.flags = GPIOHANDLE_REQUEST_INPUT;
#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_UP
    request.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
#endif
    strncpy(request.consumer_label, "lcd-hat-buttons", sizeof(request.consumer_label) - 1);

    if (ioctl(gpiochip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0) {
        fprintf(stderr, "Failed to request LCD HAT button GPIO lines: %s\n", strerror(errno));
        return -1;
    }

    return request.fd;
}

static int lcd_hat_set_line_value(int line_fd, int value) {
    struct gpiohandle_data data;

    memset(&data, 0, sizeof(data));
    data.values[0] = value ? 1 : 0;

    if (ioctl(line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        fprintf(stderr, "Failed to set GPIO line value: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static uint8_t lcd_hat_read_button_mask(lcd_hat_144_t *hat) {
    struct gpiohandle_data data;
    uint8_t mask = 0;

    if (hat->buttons_fd < 0) {
        return 0;
    }

    memset(&data, 0, sizeof(data));
    if (ioctl(hat->buttons_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        fprintf(stderr, "Failed to read LCD HAT button values: %s\n", strerror(errno));
        return 0;
    }

    for (int i = 0; i < LCD_HAT_BUTTON_COUNT; i++) {
        if (data.values[i]) {
            mask |= (uint8_t)(1U << i);
        }
    }

    return mask;
}

static int lcd_hat_spi_write(int spi_fd, const uint8_t *data, size_t length) {
    struct spi_ioc_transfer transfer;

    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (unsigned long)data;
    transfer.len = (uint32_t)length;
    transfer.speed_hz = LCD_HAT_SPI_SPEED_HZ;
    transfer.bits_per_word = LCD_HAT_SPI_BITS_PER_WORD;

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        fprintf(stderr, "SPI write failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int lcd_hat_write_command(lcd_hat_144_t *hat, uint8_t command) {
    if (lcd_hat_set_line_value(hat->dc_fd, 0) != 0) {
        return -1;
    }
    return lcd_hat_spi_write(hat->spi_fd, &command, 1);
}

static int lcd_hat_write_data(lcd_hat_144_t *hat, const uint8_t *data, size_t length) {
    if (lcd_hat_set_line_value(hat->dc_fd, 1) != 0) {
        return -1;
    }
    return lcd_hat_spi_write(hat->spi_fd, data, length);
}

static int lcd_hat_write_command_with_data(lcd_hat_144_t *hat, uint8_t command, const uint8_t *data, size_t length) {
    if (lcd_hat_write_command(hat, command) != 0) {
        return -1;
    }
    if (length > 0 && lcd_hat_write_data(hat, data, length) != 0) {
        return -1;
    }
    return 0;
}

static int lcd_hat_set_window(lcd_hat_144_t *hat, int x, int y, int width, int height) {
    uint8_t column_data[4];
    uint8_t row_data[4];
    int x_start = x + LCD_HAT_X_OFFSET;
    int x_end = x + width - 1 + LCD_HAT_X_OFFSET;
    int y_start = y + LCD_HAT_Y_OFFSET;
    int y_end = y + height - 1 + LCD_HAT_Y_OFFSET;

    column_data[0] = (uint8_t)((x_start >> 8) & 0xFF);
    column_data[1] = (uint8_t)(x_start & 0xFF);
    column_data[2] = (uint8_t)((x_end >> 8) & 0xFF);
    column_data[3] = (uint8_t)(x_end & 0xFF);

    row_data[0] = (uint8_t)((y_start >> 8) & 0xFF);
    row_data[1] = (uint8_t)(y_start & 0xFF);
    row_data[2] = (uint8_t)((y_end >> 8) & 0xFF);
    row_data[3] = (uint8_t)(y_end & 0xFF);

    if (lcd_hat_write_command_with_data(hat, 0x2A, column_data, sizeof(column_data)) != 0) {
        return -1;
    }
    if (lcd_hat_write_command_with_data(hat, 0x2B, row_data, sizeof(row_data)) != 0) {
        return -1;
    }
    if (lcd_hat_write_command(hat, 0x2C) != 0) {
        return -1;
    }

    return 0;
}

static const uint8_t *lcd_hat_get_glyph(char c) {
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t hyphen[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t period[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t slash[5] = {0x20, 0x10, 0x08, 0x04, 0x02};

    static const uint8_t digits[10][5] = {
        {0x3E, 0x51, 0x49, 0x45, 0x3E},
        {0x00, 0x42, 0x7F, 0x40, 0x00},
        {0x62, 0x51, 0x49, 0x49, 0x46},
        {0x22, 0x49, 0x49, 0x49, 0x36},
        {0x18, 0x14, 0x12, 0x7F, 0x10},
        {0x2F, 0x49, 0x49, 0x49, 0x31},
        {0x3E, 0x49, 0x49, 0x49, 0x32},
        {0x01, 0x71, 0x09, 0x05, 0x03},
        {0x36, 0x49, 0x49, 0x49, 0x36},
        {0x26, 0x49, 0x49, 0x49, 0x3E}
    };
    static const uint8_t letters[26][5] = {
        {0x7E, 0x11, 0x11, 0x11, 0x7E},
        {0x7F, 0x49, 0x49, 0x49, 0x36},
        {0x3E, 0x41, 0x41, 0x41, 0x22},
        {0x7F, 0x41, 0x41, 0x22, 0x1C},
        {0x7F, 0x49, 0x49, 0x49, 0x41},
        {0x7F, 0x09, 0x09, 0x09, 0x01},
        {0x3E, 0x41, 0x49, 0x49, 0x7A},
        {0x7F, 0x08, 0x08, 0x08, 0x7F},
        {0x00, 0x41, 0x7F, 0x41, 0x00},
        {0x20, 0x40, 0x41, 0x3F, 0x01},
        {0x7F, 0x08, 0x14, 0x22, 0x41},
        {0x7F, 0x40, 0x40, 0x40, 0x40},
        {0x7F, 0x02, 0x0C, 0x02, 0x7F},
        {0x7F, 0x04, 0x08, 0x10, 0x7F},
        {0x3E, 0x41, 0x41, 0x41, 0x3E},
        {0x7F, 0x09, 0x09, 0x09, 0x06},
        {0x3E, 0x41, 0x51, 0x21, 0x5E},
        {0x7F, 0x09, 0x19, 0x29, 0x46},
        {0x46, 0x49, 0x49, 0x49, 0x31},
        {0x01, 0x01, 0x7F, 0x01, 0x01},
        {0x3F, 0x40, 0x40, 0x40, 0x3F},
        {0x1F, 0x20, 0x40, 0x20, 0x1F},
        {0x3F, 0x40, 0x38, 0x40, 0x3F},
        {0x63, 0x14, 0x08, 0x14, 0x63},
        {0x07, 0x08, 0x70, 0x08, 0x07},
        {0x61, 0x51, 0x49, 0x45, 0x43}
    };

    if (c >= '0' && c <= '9') {
        return digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return letters[c - 'A'];
    }
    switch (c) {
        case ' ':
            return blank;
        case ':':
            return colon;
        case '-':
            return hyphen;
        case '.':
            return period;
        case '/':
            return slash;
        default:
            return blank;
    }
}

static int lcd_hat_reset_and_init_panel(lcd_hat_144_t *hat) {
    static const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    static const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    static const uint8_t invctr[] = {0x07};
    static const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    static const uint8_t pwctr2[] = {0xC5};
    static const uint8_t pwctr3[] = {0x0A, 0x00};
    static const uint8_t pwctr4[] = {0x8A, 0x2A};
    static const uint8_t pwctr5[] = {0x8A, 0xEE};
    static const uint8_t vmctr1[] = {0x0E};
    static const uint8_t madctl[] = {0x08};
    static const uint8_t colmod[] = {0x05};
    static const uint8_t gmctrp1[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    static const uint8_t gmctrn1[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};

    if (lcd_hat_set_line_value(hat->rst_fd, 1) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(5);
    if (lcd_hat_set_line_value(hat->rst_fd, 0) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(20);
    if (lcd_hat_set_line_value(hat->rst_fd, 1) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(120);

    if (lcd_hat_write_command(hat, 0x01) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(150);
    if (lcd_hat_write_command(hat, 0x11) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(150);

    if (lcd_hat_write_command_with_data(hat, 0xB1, frmctr1, sizeof(frmctr1)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xB2, frmctr1, sizeof(frmctr1)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xB3, frmctr3, sizeof(frmctr3)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xB4, invctr, sizeof(invctr)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC0, pwctr1, sizeof(pwctr1)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC1, pwctr2, sizeof(pwctr2)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC2, pwctr3, sizeof(pwctr3)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC3, pwctr4, sizeof(pwctr4)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC4, pwctr5, sizeof(pwctr5)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xC5, vmctr1, sizeof(vmctr1)) != 0 ||
        lcd_hat_write_command(hat, 0x20) != 0 ||
        lcd_hat_write_command_with_data(hat, 0x36, madctl, sizeof(madctl)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0x3A, colmod, sizeof(colmod)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xE0, gmctrp1, sizeof(gmctrp1)) != 0 ||
        lcd_hat_write_command_with_data(hat, 0xE1, gmctrn1, sizeof(gmctrn1)) != 0) {
        return -1;
    }

    if (lcd_hat_write_command(hat, 0x13) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(10);
    if (lcd_hat_write_command(hat, 0x29) != 0) {
        return -1;
    }
    lcd_hat_sleep_ms(100);

    return 0;
}

int lcd_hat_144_init(lcd_hat_144_t *hat, const char *spi_device, const char *gpiochip_device) {
    uint8_t mode = LCD_HAT_SPI_MODE;
    uint8_t bits = LCD_HAT_SPI_BITS_PER_WORD;
    uint32_t speed = LCD_HAT_SPI_SPEED_HZ;

    memset(hat, 0, sizeof(*hat));
    hat->spi_fd = -1;
    hat->gpiochip_fd = -1;
    hat->dc_fd = -1;
    hat->rst_fd = -1;
    hat->bl_fd = -1;
    hat->buttons_fd = -1;
    hat->idle_button_mask = 0;
    hat->last_button = LCD_HAT_BUTTON_NONE;

    hat->spi_fd = open(spi_device, O_RDWR);
    if (hat->spi_fd < 0) {
        fprintf(stderr, "Failed to open SPI device %s: %s\n", spi_device, strerror(errno));
        return -1;
    }

    if (ioctl(hat->spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(hat->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(hat->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        fprintf(stderr, "Failed to configure SPI device %s: %s\n", spi_device, strerror(errno));
        lcd_hat_144_close(hat);
        return -1;
    }

    hat->gpiochip_fd = open(gpiochip_device, O_RDONLY);
    if (hat->gpiochip_fd < 0) {
        fprintf(stderr, "Failed to open GPIO chip %s: %s\n", gpiochip_device, strerror(errno));
        lcd_hat_144_close(hat);
        return -1;
    }

    hat->dc_fd = lcd_hat_request_output_line(hat->gpiochip_fd, LCD_HAT_GPIO_DC, 1, "lcd-hat-dc");
    hat->rst_fd = lcd_hat_request_output_line(hat->gpiochip_fd, LCD_HAT_GPIO_RST, 1, "lcd-hat-rst");
    hat->bl_fd = lcd_hat_request_output_line(hat->gpiochip_fd, LCD_HAT_GPIO_BL, 1, "lcd-hat-bl");
    hat->buttons_fd = lcd_hat_request_buttons(hat->gpiochip_fd);

    if (hat->dc_fd < 0 || hat->rst_fd < 0 || hat->bl_fd < 0 || hat->buttons_fd < 0) {
        lcd_hat_144_close(hat);
        return -1;
    }

    if (lcd_hat_reset_and_init_panel(hat) != 0) {
        lcd_hat_144_close(hat);
        return -1;
    }

    hat->idle_button_mask = lcd_hat_read_button_mask(hat);
    lcd_hat_144_set_backlight(hat, 1);
    return 0;
}

void lcd_hat_144_close(lcd_hat_144_t *hat) {
    if (hat->buttons_fd >= 0) {
        close(hat->buttons_fd);
    }
    if (hat->bl_fd >= 0) {
        close(hat->bl_fd);
    }
    if (hat->rst_fd >= 0) {
        close(hat->rst_fd);
    }
    if (hat->dc_fd >= 0) {
        close(hat->dc_fd);
    }
    if (hat->gpiochip_fd >= 0) {
        close(hat->gpiochip_fd);
    }
    if (hat->spi_fd >= 0) {
        close(hat->spi_fd);
    }

    hat->buttons_fd = -1;
    hat->bl_fd = -1;
    hat->rst_fd = -1;
    hat->dc_fd = -1;
    hat->gpiochip_fd = -1;
    hat->spi_fd = -1;
    hat->idle_button_mask = 0;
}

void lcd_hat_144_set_backlight(lcd_hat_144_t *hat, int enabled) {
    if (hat->bl_fd >= 0) {
        lcd_hat_set_line_value(hat->bl_fd, enabled ? 1 : 0);
    }
}

void lcd_hat_144_fill_rect(lcd_hat_144_t *hat, int x, int y, int width, int height, uint16_t color) {
    uint8_t chunk[128];
    int pixels_remaining;

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return;
    }
    if (x >= LCD_HAT_144_WIDTH || y >= LCD_HAT_144_HEIGHT) {
        return;
    }
    if (x + width > LCD_HAT_144_WIDTH) {
        width = LCD_HAT_144_WIDTH - x;
    }
    if (y + height > LCD_HAT_144_HEIGHT) {
        height = LCD_HAT_144_HEIGHT - y;
    }

    if (lcd_hat_set_window(hat, x, y, width, height) != 0) {
        return;
    }

    for (size_t i = 0; i < sizeof(chunk); i += 2) {
        chunk[i] = (uint8_t)(color >> 8);
        chunk[i + 1] = (uint8_t)(color & 0xFF);
    }

    pixels_remaining = width * height;
    while (pixels_remaining > 0) {
        int pixels_in_chunk = pixels_remaining;
        size_t bytes_to_write;

        if (pixels_in_chunk > (int)(sizeof(chunk) / 2)) {
            pixels_in_chunk = (int)(sizeof(chunk) / 2);
        }

        bytes_to_write = (size_t)pixels_in_chunk * 2;
        if (lcd_hat_write_data(hat, chunk, bytes_to_write) != 0) {
            return;
        }
        pixels_remaining -= pixels_in_chunk;
    }
}

void lcd_hat_144_fill_screen(lcd_hat_144_t *hat, uint16_t color) {
    lcd_hat_144_fill_rect(hat, 0, 0, LCD_HAT_144_WIDTH, LCD_HAT_144_HEIGHT, color);
}

void lcd_hat_144_draw_text(lcd_hat_144_t *hat, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale) {
    int cursor_x = x;

    if (scale <= 0) {
        scale = 1;
    }

    while (*text != '\0') {
        const uint8_t *glyph = lcd_hat_get_glyph(*text);

        for (int column = 0; column < 5; column++) {
            uint8_t bits = glyph[column];
            for (int row = 0; row < 7; row++) {
                uint16_t color = (bits & (1U << row)) ? fg : bg;
                lcd_hat_144_fill_rect(hat,
                                      cursor_x + (column * scale),
                                      y + (row * scale),
                                      scale,
                                      scale,
                                      color);
            }
        }

        lcd_hat_144_fill_rect(hat, cursor_x + (5 * scale), y, scale, 7 * scale, bg);
        cursor_x += 6 * scale;
        text++;
    }
}

lcd_hat_button_t lcd_hat_144_poll_button(lcd_hat_144_t *hat) {
    uint8_t current_mask;
    uint8_t changed_mask;

    if (hat->buttons_fd < 0) {
        return LCD_HAT_BUTTON_NONE;
    }

    current_mask = lcd_hat_read_button_mask(hat);
    changed_mask = (uint8_t)(current_mask ^ hat->idle_button_mask);

    for (int i = 0; i < LCD_HAT_BUTTON_COUNT; i++) {
        if (changed_mask & (uint8_t)(1U << i)) {
            return LCD_HAT_BUTTON_IDS[i];
        }
    }

    return LCD_HAT_BUTTON_NONE;
}

const char *lcd_hat_144_button_name(lcd_hat_button_t button) {
    switch (button) {
        case LCD_HAT_BUTTON_KEY1:
            return "KEY1";
        case LCD_HAT_BUTTON_KEY2:
            return "KEY2";
        case LCD_HAT_BUTTON_KEY3:
            return "KEY3";
        case LCD_HAT_BUTTON_UP:
            return "UP";
        case LCD_HAT_BUTTON_DOWN:
            return "DOWN";
        case LCD_HAT_BUTTON_LEFT:
            return "LEFT";
        case LCD_HAT_BUTTON_RIGHT:
            return "RIGHT";
        case LCD_HAT_BUTTON_PRESS:
            return "PRESS";
        case LCD_HAT_BUTTON_NONE:
        default:
            return "NONE";
    }
}

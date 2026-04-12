#include "pirate_audio_hat.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/gpio.h>
#include <linux/spi/spidev.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define PIRATE_AUDIO_SPI_MODE SPI_MODE_0
#define PIRATE_AUDIO_SPI_BITS_PER_WORD 8
#define PIRATE_AUDIO_SPI_SPEED_HZ 8000000U

#define PIRATE_AUDIO_GPIO_DC 9
#define PIRATE_AUDIO_GPIO_BL 13
#define PIRATE_AUDIO_GPIO_RESET 25

#define PIRATE_AUDIO_BUTTON_COUNT 4

static const unsigned int PIRATE_AUDIO_BUTTON_LINES[PIRATE_AUDIO_BUTTON_COUNT] = {5, 6, 16, 24};
static const pirate_audio_button_t PIRATE_AUDIO_BUTTON_IDS[PIRATE_AUDIO_BUTTON_COUNT] = {
    PIRATE_AUDIO_BUTTON_A,
    PIRATE_AUDIO_BUTTON_B,
    PIRATE_AUDIO_BUTTON_X,
    PIRATE_AUDIO_BUTTON_Y
};

static int pirate_audio_sleep_ms(long milliseconds) {
    struct timespec duration;
    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    return nanosleep(&duration, NULL);
}

static int pirate_audio_request_output_line(int gpiochip_fd, unsigned int offset, int initial_value, const char *consumer) {
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

static int pirate_audio_request_buttons(int gpiochip_fd) {
    struct gpiohandle_request request;

    memset(&request, 0, sizeof(request));
    request.lines = PIRATE_AUDIO_BUTTON_COUNT;
    for (int i = 0; i < PIRATE_AUDIO_BUTTON_COUNT; i++) {
        request.lineoffsets[i] = PIRATE_AUDIO_BUTTON_LINES[i];
    }

    request.flags = GPIOHANDLE_REQUEST_INPUT | GPIOHANDLE_REQUEST_ACTIVE_LOW;
#ifdef GPIOHANDLE_REQUEST_BIAS_PULL_UP
    request.flags |= GPIOHANDLE_REQUEST_BIAS_PULL_UP;
#endif
    strncpy(request.consumer_label, "pirate-audio-buttons", sizeof(request.consumer_label) - 1);

    if (ioctl(gpiochip_fd, GPIO_GET_LINEHANDLE_IOCTL, &request) < 0) {
        fprintf(stderr, "Failed to request Pirate Audio button GPIO lines: %s\n", strerror(errno));
        return -1;
    }

    return request.fd;
}

static int pirate_audio_set_line_value(int line_fd, int value) {
    struct gpiohandle_data data;

    memset(&data, 0, sizeof(data));
    data.values[0] = value ? 1 : 0;

    if (ioctl(line_fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data) < 0) {
        fprintf(stderr, "Failed to set GPIO line value: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int pirate_audio_spi_write(int spi_fd, const uint8_t *data, size_t length) {
    struct spi_ioc_transfer transfer;

    memset(&transfer, 0, sizeof(transfer));
    transfer.tx_buf = (unsigned long)data;
    transfer.len = (uint32_t)length;
    transfer.speed_hz = PIRATE_AUDIO_SPI_SPEED_HZ;
    transfer.bits_per_word = PIRATE_AUDIO_SPI_BITS_PER_WORD;

    if (ioctl(spi_fd, SPI_IOC_MESSAGE(1), &transfer) < 0) {
        fprintf(stderr, "SPI write failed: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static int pirate_audio_write_command(pirate_audio_hat_t *hat, uint8_t command) {
    if (pirate_audio_set_line_value(hat->dc_fd, 0) != 0) {
        return -1;
    }
    return pirate_audio_spi_write(hat->spi_fd, &command, 1);
}

static int pirate_audio_write_data(pirate_audio_hat_t *hat, const uint8_t *data, size_t length) {
    if (pirate_audio_set_line_value(hat->dc_fd, 1) != 0) {
        return -1;
    }
    return pirate_audio_spi_write(hat->spi_fd, data, length);
}

static int pirate_audio_write_command_with_data(pirate_audio_hat_t *hat, uint8_t command, const uint8_t *data, size_t length) {
    if (pirate_audio_write_command(hat, command) != 0) {
        return -1;
    }
    if (length > 0 && pirate_audio_write_data(hat, data, length) != 0) {
        return -1;
    }
    return 0;
}

static int pirate_audio_hard_reset(pirate_audio_hat_t *hat) {
    if (hat->reset_fd < 0) {
        return 0;
    }

    if (pirate_audio_set_line_value(hat->reset_fd, 1) != 0) {
        return -1;
    }
    pirate_audio_sleep_ms(10);
    if (pirate_audio_set_line_value(hat->reset_fd, 0) != 0) {
        return -1;
    }
    pirate_audio_sleep_ms(10);
    if (pirate_audio_set_line_value(hat->reset_fd, 1) != 0) {
        return -1;
    }
    pirate_audio_sleep_ms(120);
    return 0;
}

static int pirate_audio_set_madctl(pirate_audio_hat_t *hat, uint8_t madctl) {
    return pirate_audio_write_command_with_data(hat, 0x36, &madctl, 1);
}

static int pirate_audio_set_window(pirate_audio_hat_t *hat, int x, int y, int width, int height) {
    uint8_t column_data[4];
    uint8_t row_data[4];
    int x_end = x + width - 1;
    int y_end = y + height - 1;

    column_data[0] = (uint8_t)((x >> 8) & 0xFF);
    column_data[1] = (uint8_t)(x & 0xFF);
    column_data[2] = (uint8_t)((x_end >> 8) & 0xFF);
    column_data[3] = (uint8_t)(x_end & 0xFF);

    row_data[0] = (uint8_t)((y >> 8) & 0xFF);
    row_data[1] = (uint8_t)(y & 0xFF);
    row_data[2] = (uint8_t)((y_end >> 8) & 0xFF);
    row_data[3] = (uint8_t)(y_end & 0xFF);

    if (pirate_audio_write_command_with_data(hat, 0x2A, column_data, sizeof(column_data)) != 0) {
        return -1;
    }
    if (pirate_audio_write_command_with_data(hat, 0x2B, row_data, sizeof(row_data)) != 0) {
        return -1;
    }
    if (pirate_audio_write_command(hat, 0x2C) != 0) {
        return -1;
    }

    return 0;
}

static const uint8_t *pirate_audio_get_glyph(char c) {
    static const uint8_t blank[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t hyphen[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t period[5] = {0x00, 0x60, 0x60, 0x00, 0x00};

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
        default:
            return blank;
    }
}

static int pirate_audio_init_panel(pirate_audio_hat_t *hat) {
    static const uint8_t porch[] = {0x0C, 0x0C, 0x00, 0x33, 0x33};
    static const uint8_t gate[] = {0x35};
    static const uint8_t vcom[] = {0x19};
    static const uint8_t lcm[] = {0x2C};
    static const uint8_t vdv_vrh[] = {0x01};
    static const uint8_t vrh[] = {0x12};
    static const uint8_t vdv[] = {0x20};
    static const uint8_t frame[] = {0x0F};
    static const uint8_t power[] = {0xA4, 0xA1};
    static const uint8_t color_mode[] = {0x55};
    static const uint8_t madctl[] = {0xA8};
    static const uint8_t gamma_positive[] = {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54, 0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23};
    static const uint8_t gamma_negative[] = {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44, 0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23};

    if (pirate_audio_write_command(hat, 0x01) != 0) {
        return -1;
    }
    pirate_audio_sleep_ms(150);
    if (pirate_audio_write_command(hat, 0x11) != 0) {
        return -1;
    }
    pirate_audio_sleep_ms(150);

    if (pirate_audio_write_command_with_data(hat, 0xB2, porch, sizeof(porch)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xB7, gate, sizeof(gate)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xBB, vcom, sizeof(vcom)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xC0, lcm, sizeof(lcm)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xC2, vdv_vrh, sizeof(vdv_vrh)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xC3, vrh, sizeof(vrh)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xC4, vdv, sizeof(vdv)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xC6, frame, sizeof(frame)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xD0, power, sizeof(power)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xE0, gamma_positive, sizeof(gamma_positive)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0xE1, gamma_negative, sizeof(gamma_negative)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0x3A, color_mode, sizeof(color_mode)) != 0 ||
        pirate_audio_write_command_with_data(hat, 0x36, madctl, sizeof(madctl)) != 0 ||
        pirate_audio_write_command(hat, 0x21) != 0 ||
        pirate_audio_write_command(hat, 0x13) != 0 ||
        pirate_audio_write_command(hat, 0x29) != 0) {
        return -1;
    }

    pirate_audio_sleep_ms(100);
    return 0;
}

int pirate_audio_hat_init(pirate_audio_hat_t *hat, const char *spi_device, const char *gpiochip_device) {
    uint8_t mode = PIRATE_AUDIO_SPI_MODE;
    uint8_t bits = PIRATE_AUDIO_SPI_BITS_PER_WORD;
    uint32_t speed = PIRATE_AUDIO_SPI_SPEED_HZ;

    memset(hat, 0, sizeof(*hat));
    hat->spi_fd = -1;
    hat->gpiochip_fd = -1;
    hat->dc_fd = -1;
    hat->bl_fd = -1;
    hat->reset_fd = -1;
    hat->buttons_fd = -1;

    hat->spi_fd = open(spi_device, O_RDWR);
    if (hat->spi_fd < 0) {
        fprintf(stderr, "Failed to open SPI device %s: %s\n", spi_device, strerror(errno));
        return -1;
    }

    if (ioctl(hat->spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(hat->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(hat->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        fprintf(stderr, "Failed to configure SPI device %s: %s\n", spi_device, strerror(errno));
        pirate_audio_hat_close(hat);
        return -1;
    }

    hat->gpiochip_fd = open(gpiochip_device, O_RDONLY);
    if (hat->gpiochip_fd < 0) {
        fprintf(stderr, "Failed to open GPIO chip %s: %s\n", gpiochip_device, strerror(errno));
        pirate_audio_hat_close(hat);
        return -1;
    }

    hat->dc_fd = pirate_audio_request_output_line(hat->gpiochip_fd, PIRATE_AUDIO_GPIO_DC, 1, "pirate-audio-dc");
    hat->bl_fd = pirate_audio_request_output_line(hat->gpiochip_fd, PIRATE_AUDIO_GPIO_BL, 0, "pirate-audio-bl");
    hat->reset_fd = pirate_audio_request_output_line(hat->gpiochip_fd, PIRATE_AUDIO_GPIO_RESET, 1, "pirate-audio-reset");
    hat->buttons_fd = pirate_audio_request_buttons(hat->gpiochip_fd);

    if (hat->dc_fd < 0 || hat->bl_fd < 0 || hat->reset_fd < 0 || hat->buttons_fd < 0) {
        pirate_audio_hat_close(hat);
        return -1;
    }

    if (pirate_audio_hard_reset(hat) != 0) {
        pirate_audio_hat_close(hat);
        return -1;
    }

    if (pirate_audio_init_panel(hat) != 0) {
        pirate_audio_hat_close(hat);
        return -1;
    }

    pirate_audio_hat_set_backlight(hat, 1);
    return 0;
}

void pirate_audio_hat_close(pirate_audio_hat_t *hat) {
    if (hat->buttons_fd >= 0) {
        close(hat->buttons_fd);
    }
    if (hat->reset_fd >= 0) {
        close(hat->reset_fd);
    }
    if (hat->bl_fd >= 0) {
        close(hat->bl_fd);
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
}

void pirate_audio_hat_set_backlight(pirate_audio_hat_t *hat, int enabled) {
    if (hat->bl_fd >= 0) {
        pirate_audio_set_line_value(hat->bl_fd, enabled ? 1 : 0);
    }
}

void pirate_audio_hat_set_rotation(pirate_audio_hat_t *hat, int rotation) {
    static const uint8_t rotation_values[4] = {
        0xC8,
        0xA8,
        0x08,
        0x68
    };

    int normalized = rotation % 4;
    if (normalized < 0) {
        normalized += 4;
    }

    pirate_audio_set_madctl(hat, rotation_values[normalized]);
}

void pirate_audio_hat_fill_rect(pirate_audio_hat_t *hat, int x, int y, int width, int height, uint16_t color) {
    uint8_t chunk[256];
    int pixels_remaining;

    if (x < 0 || y < 0 || width <= 0 || height <= 0) {
        return;
    }
    if (x >= PIRATE_AUDIO_WIDTH || y >= PIRATE_AUDIO_HEIGHT) {
        return;
    }
    if (x + width > PIRATE_AUDIO_WIDTH) {
        width = PIRATE_AUDIO_WIDTH - x;
    }
    if (y + height > PIRATE_AUDIO_HEIGHT) {
        height = PIRATE_AUDIO_HEIGHT - y;
    }

    if (pirate_audio_set_window(hat, x, y, width, height) != 0) {
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
        if (pirate_audio_write_data(hat, chunk, bytes_to_write) != 0) {
            return;
        }
        pixels_remaining -= pixels_in_chunk;
    }
}

void pirate_audio_hat_fill_screen(pirate_audio_hat_t *hat, uint16_t color) {
    pirate_audio_hat_fill_rect(hat, 0, 0, PIRATE_AUDIO_WIDTH, PIRATE_AUDIO_HEIGHT, color);
}

void pirate_audio_hat_draw_text(pirate_audio_hat_t *hat, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale) {
    int cursor_x = x;

    if (scale <= 0) {
        scale = 1;
    }

    while (*text != '\0') {
        const uint8_t *glyph = pirate_audio_get_glyph(*text);

        for (int column = 0; column < 5; column++) {
            uint8_t bits = glyph[column];
            for (int row = 0; row < 7; row++) {
                uint16_t color = (bits & (1U << row)) ? fg : bg;
                pirate_audio_hat_fill_rect(hat,
                                           cursor_x + (column * scale),
                                           y + (row * scale),
                                           scale,
                                           scale,
                                           color);
            }
        }

        pirate_audio_hat_fill_rect(hat, cursor_x + (5 * scale), y, scale, 7 * scale, bg);
        cursor_x += 6 * scale;
        text++;
    }
}

pirate_audio_button_t pirate_audio_hat_poll_button(pirate_audio_hat_t *hat) {
    struct gpiohandle_data data;

    if (hat->buttons_fd < 0) {
        return PIRATE_AUDIO_BUTTON_NONE;
    }

    memset(&data, 0, sizeof(data));
    if (ioctl(hat->buttons_fd, GPIOHANDLE_GET_LINE_VALUES_IOCTL, &data) < 0) {
        fprintf(stderr, "Failed to read Pirate Audio button values: %s\n", strerror(errno));
        return PIRATE_AUDIO_BUTTON_NONE;
    }

    for (int i = 0; i < PIRATE_AUDIO_BUTTON_COUNT; i++) {
        if (data.values[i]) {
            return PIRATE_AUDIO_BUTTON_IDS[i];
        }
    }

    return PIRATE_AUDIO_BUTTON_NONE;
}

const char *pirate_audio_button_name(pirate_audio_button_t button) {
    switch (button) {
        case PIRATE_AUDIO_BUTTON_A:
            return "BUTTON A";
        case PIRATE_AUDIO_BUTTON_B:
            return "BUTTON B";
        case PIRATE_AUDIO_BUTTON_X:
            return "BUTTON X";
        case PIRATE_AUDIO_BUTTON_Y:
            return "BUTTON Y";
        case PIRATE_AUDIO_BUTTON_NONE:
        default:
            return "NONE";
    }
}

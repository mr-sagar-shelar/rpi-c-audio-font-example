#ifndef PIRATE_AUDIO_HAT_H
#define PIRATE_AUDIO_HAT_H

#include <stdint.h>

#define PIRATE_AUDIO_WIDTH 240
#define PIRATE_AUDIO_HEIGHT 240

typedef enum {
    PIRATE_AUDIO_BUTTON_NONE = 0,
    PIRATE_AUDIO_BUTTON_A,
    PIRATE_AUDIO_BUTTON_B,
    PIRATE_AUDIO_BUTTON_X,
    PIRATE_AUDIO_BUTTON_Y
} pirate_audio_button_t;

typedef struct {
    int spi_fd;
    int gpiochip_fd;
    int dc_fd;
    int bl_fd;
    int dac_enable_fd;
    int buttons_fd;
} pirate_audio_hat_t;

int pirate_audio_hat_init(pirate_audio_hat_t *hat, const char *spi_device, const char *gpiochip_device);
void pirate_audio_hat_close(pirate_audio_hat_t *hat);
void pirate_audio_hat_set_backlight(pirate_audio_hat_t *hat, int enabled);
void pirate_audio_hat_fill_screen(pirate_audio_hat_t *hat, uint16_t color);
void pirate_audio_hat_fill_rect(pirate_audio_hat_t *hat, int x, int y, int width, int height, uint16_t color);
void pirate_audio_hat_draw_text(pirate_audio_hat_t *hat, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
pirate_audio_button_t pirate_audio_hat_poll_button(pirate_audio_hat_t *hat);
const char *pirate_audio_button_name(pirate_audio_button_t button);

#define PIRATE_AUDIO_COLOR_BLACK 0x0000
#define PIRATE_AUDIO_COLOR_WHITE 0xFFFF
#define PIRATE_AUDIO_COLOR_RED 0xF800
#define PIRATE_AUDIO_COLOR_GREEN 0x07E0
#define PIRATE_AUDIO_COLOR_BLUE 0x001F
#define PIRATE_AUDIO_COLOR_YELLOW 0xFFE0
#define PIRATE_AUDIO_COLOR_CYAN 0x07FF
#define PIRATE_AUDIO_COLOR_MAGENTA 0xF81F
#define PIRATE_AUDIO_COLOR_ORANGE 0xFD20
#define PIRATE_AUDIO_COLOR_NAVY 0x0010

#endif

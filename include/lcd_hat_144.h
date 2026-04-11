#ifndef LCD_HAT_144_H
#define LCD_HAT_144_H

#include <stdint.h>

#define LCD_HAT_144_WIDTH 128
#define LCD_HAT_144_HEIGHT 128

typedef enum {
    LCD_HAT_BUTTON_NONE = 0,
    LCD_HAT_BUTTON_KEY1,
    LCD_HAT_BUTTON_KEY2,
    LCD_HAT_BUTTON_KEY3,
    LCD_HAT_BUTTON_UP,
    LCD_HAT_BUTTON_DOWN,
    LCD_HAT_BUTTON_LEFT,
    LCD_HAT_BUTTON_RIGHT,
    LCD_HAT_BUTTON_PRESS
} lcd_hat_button_t;

typedef struct {
    int spi_fd;
    int gpiochip_fd;
    int dc_fd;
    int rst_fd;
    int bl_fd;
    int buttons_fd;
    lcd_hat_button_t last_button;
} lcd_hat_144_t;

int lcd_hat_144_init(lcd_hat_144_t *hat, const char *spi_device, const char *gpiochip_device);
void lcd_hat_144_close(lcd_hat_144_t *hat);
void lcd_hat_144_set_backlight(lcd_hat_144_t *hat, int enabled);
void lcd_hat_144_fill_screen(lcd_hat_144_t *hat, uint16_t color);
void lcd_hat_144_fill_rect(lcd_hat_144_t *hat, int x, int y, int width, int height, uint16_t color);
void lcd_hat_144_draw_text(lcd_hat_144_t *hat, int x, int y, const char *text, uint16_t fg, uint16_t bg, int scale);
lcd_hat_button_t lcd_hat_144_poll_button(lcd_hat_144_t *hat);
const char *lcd_hat_144_button_name(lcd_hat_button_t button);

#define LCD_HAT_COLOR_BLACK 0x0000
#define LCD_HAT_COLOR_WHITE 0xFFFF
#define LCD_HAT_COLOR_RED 0xF800
#define LCD_HAT_COLOR_GREEN 0x07E0
#define LCD_HAT_COLOR_BLUE 0x001F
#define LCD_HAT_COLOR_YELLOW 0xFFE0
#define LCD_HAT_COLOR_CYAN 0x07FF
#define LCD_HAT_COLOR_MAGENTA 0xF81F
#define LCD_HAT_COLOR_ORANGE 0xFD20
#define LCD_HAT_COLOR_NAVY 0x0010

#endif

#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "lcd_hat_144.h"

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static void sleep_for_ms(long milliseconds) {
    struct timespec duration;

    duration.tv_sec = milliseconds / 1000;
    duration.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&duration, NULL);
}

static void draw_status_screen(lcd_hat_144_t *hat, const char *message) {
    lcd_hat_144_fill_screen(hat, LCD_HAT_COLOR_NAVY);
    lcd_hat_144_fill_rect(hat, 6, 6, 116, 28, LCD_HAT_COLOR_BLUE);
    lcd_hat_144_draw_text(hat, 12, 14, "LCD HAT DEMO", LCD_HAT_COLOR_WHITE, LCD_HAT_COLOR_BLUE, 2);

    lcd_hat_144_fill_rect(hat, 8, 44, 112, 22, LCD_HAT_COLOR_BLACK);
    lcd_hat_144_draw_text(hat, 12, 50, "PRESS A BUTTON", LCD_HAT_COLOR_YELLOW, LCD_HAT_COLOR_BLACK, 1);

    lcd_hat_144_fill_rect(hat, 8, 74, 112, 22, LCD_HAT_COLOR_BLACK);
    lcd_hat_144_draw_text(hat, 12, 80, "LAST:", LCD_HAT_COLOR_CYAN, LCD_HAT_COLOR_BLACK, 2);
    lcd_hat_144_draw_text(hat, 60, 80, message, LCD_HAT_COLOR_WHITE, LCD_HAT_COLOR_BLACK, 2);

    lcd_hat_144_fill_rect(hat, 8, 104, 112, 16, LCD_HAT_COLOR_BLACK);
    lcd_hat_144_draw_text(hat, 12, 108, "KEY1 KEY2 KEY3", LCD_HAT_COLOR_GREEN, LCD_HAT_COLOR_BLACK, 1);
    lcd_hat_144_draw_text(hat, 18, 118, "JOY: U D L R P", LCD_HAT_COLOR_GREEN, LCD_HAT_COLOR_BLACK, 1);
}

int main(void) {
    lcd_hat_144_t hat;
    lcd_hat_button_t current_button = LCD_HAT_BUTTON_NONE;
    lcd_hat_button_t last_reported_button = LCD_HAT_BUTTON_NONE;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (lcd_hat_144_init(&hat, "/dev/spidev0.0", "/dev/gpiochip0") != 0) {
        fprintf(stderr, "Failed to initialize the 1.44 inch LCD HAT.\n");
        fprintf(stderr, "Make sure SPI is enabled and config.txt contains gpio=6,19,5,26,13,21,20,16=pu\n");
        return 1;
    }

    draw_status_screen(&hat, "NONE");
    printf("LCD HAT demo started. Press Ctrl+C to exit.\n");

    while (keep_running) {
        current_button = lcd_hat_144_poll_button(&hat);
        if (current_button != LCD_HAT_BUTTON_NONE && current_button != last_reported_button) {
            const char *button_name = lcd_hat_144_button_name(current_button);
            char message[32];

            snprintf(message, sizeof(message), "%s", button_name);
            draw_status_screen(&hat, message);
            printf("Button pressed: %s\n", button_name);
            last_reported_button = current_button;
        } else if (current_button == LCD_HAT_BUTTON_NONE) {
            last_reported_button = LCD_HAT_BUTTON_NONE;
        }

        sleep_for_ms(60);
    }

    lcd_hat_144_fill_screen(&hat, LCD_HAT_COLOR_BLACK);
    lcd_hat_144_draw_text(&hat, 20, 56, "GOODBYE", LCD_HAT_COLOR_WHITE, LCD_HAT_COLOR_BLACK, 2);
    sleep_for_ms(200);
    lcd_hat_144_close(&hat);
    return 0;
}

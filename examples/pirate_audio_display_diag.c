#include <signal.h>
#include <stdio.h>
#include <time.h>

#include "pirate_audio_hat.h"

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

static void draw_rotation_pattern(pirate_audio_hat_t *hat, int rotation) {
    const int half_w = PIRATE_AUDIO_WIDTH / 2;
    const int half_h = PIRATE_AUDIO_HEIGHT / 2;

    pirate_audio_hat_set_rotation(hat, rotation);
    pirate_audio_hat_fill_screen(hat, PIRATE_AUDIO_COLOR_BLACK);

    pirate_audio_hat_fill_rect(hat, 0, 0, half_w, half_h, PIRATE_AUDIO_COLOR_RED);
    pirate_audio_hat_fill_rect(hat, half_w, 0, half_w, half_h, PIRATE_AUDIO_COLOR_GREEN);
    pirate_audio_hat_fill_rect(hat, 0, half_h, half_w, half_h, PIRATE_AUDIO_COLOR_BLUE);
    pirate_audio_hat_fill_rect(hat, half_w, half_h, half_w, half_h, PIRATE_AUDIO_COLOR_YELLOW);

    pirate_audio_hat_fill_rect(hat, 8, 8, 24, 24, PIRATE_AUDIO_COLOR_WHITE);
    pirate_audio_hat_fill_rect(hat, PIRATE_AUDIO_WIDTH - 32, 8, 24, 24, PIRATE_AUDIO_COLOR_CYAN);
    pirate_audio_hat_fill_rect(hat, 8, PIRATE_AUDIO_HEIGHT - 32, 24, 24, PIRATE_AUDIO_COLOR_MAGENTA);
    pirate_audio_hat_fill_rect(hat, PIRATE_AUDIO_WIDTH - 32, PIRATE_AUDIO_HEIGHT - 32, 24, 24, PIRATE_AUDIO_COLOR_ORANGE);

    pirate_audio_hat_fill_rect(hat, 110, 0, 20, PIRATE_AUDIO_HEIGHT, PIRATE_AUDIO_COLOR_WHITE);
    pirate_audio_hat_fill_rect(hat, 0, 110, PIRATE_AUDIO_WIDTH, 20, PIRATE_AUDIO_COLOR_WHITE);
}

int main(int argc, char **argv) {
    const char *spi_device = argc > 1 ? argv[1] : "/dev/spidev0.1";
    const char *gpiochip_device = argc > 2 ? argv[2] : "/dev/gpiochip0";
    pirate_audio_hat_t hat;
    int rotation = 0;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (pirate_audio_hat_init(&hat, spi_device, gpiochip_device) != 0) {
        fprintf(stderr, "Failed to initialize Pirate Audio display diagnostics.\n");
        return 1;
    }

    printf("Pirate Audio display diagnostic\n");
    printf("A=rotation 0, B=rotation 1, X=rotation 2, Y=rotation 3\n");
    printf("Press Ctrl+C to exit.\n");

    draw_rotation_pattern(&hat, rotation);

    while (keep_running) {
        pirate_audio_button_t button = pirate_audio_hat_poll_button(&hat);

        switch (button) {
            case PIRATE_AUDIO_BUTTON_A:
                rotation = 0;
                draw_rotation_pattern(&hat, rotation);
                printf("Rotation set to 0\n");
                sleep_for_ms(220);
                break;
            case PIRATE_AUDIO_BUTTON_B:
                rotation = 1;
                draw_rotation_pattern(&hat, rotation);
                printf("Rotation set to 1\n");
                sleep_for_ms(220);
                break;
            case PIRATE_AUDIO_BUTTON_X:
                rotation = 2;
                draw_rotation_pattern(&hat, rotation);
                printf("Rotation set to 2\n");
                sleep_for_ms(220);
                break;
            case PIRATE_AUDIO_BUTTON_Y:
                rotation = 3;
                draw_rotation_pattern(&hat, rotation);
                printf("Rotation set to 3\n");
                sleep_for_ms(220);
                break;
            case PIRATE_AUDIO_BUTTON_NONE:
            default:
                sleep_for_ms(50);
                break;
        }
    }

    pirate_audio_hat_fill_screen(&hat, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_close(&hat);
    return 0;
}

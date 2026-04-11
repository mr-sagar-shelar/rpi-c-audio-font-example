#include <signal.h>
#include <stdio.h>
#include <time.h>

#include "demo_audio.h"
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

static double pirate_audio_button_tone(pirate_audio_button_t button) {
    switch (button) {
        case PIRATE_AUDIO_BUTTON_A:
            return 261.63;
        case PIRATE_AUDIO_BUTTON_B:
            return 329.63;
        case PIRATE_AUDIO_BUTTON_X:
            return 392.00;
        case PIRATE_AUDIO_BUTTON_Y:
            return 523.25;
        case PIRATE_AUDIO_BUTTON_NONE:
        default:
            return 0.0;
    }
}

static void draw_pirate_audio_screen(pirate_audio_hat_t *hat, const char *message) {
    pirate_audio_hat_fill_screen(hat, PIRATE_AUDIO_COLOR_NAVY);
    pirate_audio_hat_fill_rect(hat, 10, 10, 220, 42, PIRATE_AUDIO_COLOR_BLUE);
    pirate_audio_hat_draw_text(hat, 22, 22, "PIRATE AUDIO", PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLUE, 3);

    pirate_audio_hat_fill_rect(hat, 16, 72, 208, 32, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 26, 82, "PRESS A B X Y", PIRATE_AUDIO_COLOR_YELLOW, PIRATE_AUDIO_COLOR_BLACK, 2);

    pirate_audio_hat_fill_rect(hat, 16, 122, 208, 48, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 32, 132, "LAST:", PIRATE_AUDIO_COLOR_CYAN, PIRATE_AUDIO_COLOR_BLACK, 3);
    pirate_audio_hat_draw_text(hat, 32, 156, message, PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLACK, 2);

    pirate_audio_hat_fill_rect(hat, 16, 196, 208, 28, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 24, 204, "SPEAKER + SCREEN DEMO", PIRATE_AUDIO_COLOR_GREEN, PIRATE_AUDIO_COLOR_BLACK, 1);
}

int main(int argc, char **argv) {
    const char *spi_device = argc > 1 ? argv[1] : "/dev/spidev0.1";
    const char *gpiochip_device = argc > 2 ? argv[2] : "/dev/gpiochip0";
    const char *audio_device = argc > 3 ? argv[3] : "default";
    pirate_audio_hat_t hat;
    pirate_audio_button_t current_button = PIRATE_AUDIO_BUTTON_NONE;
    pirate_audio_button_t last_reported_button = PIRATE_AUDIO_BUTTON_NONE;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (pirate_audio_hat_init(&hat, spi_device, gpiochip_device) != 0) {
        fprintf(stderr, "Failed to initialize the Pirate Audio speaker HAT.\n");
        fprintf(stderr, "Try building the TinyCore image with HARDWARE_PROFILE=pirate-audio-speaker\n");
        return 1;
    }

    draw_pirate_audio_screen(&hat, "NONE");
    printf("Pirate Audio demo started. Press Ctrl+C to exit.\n");

    while (keep_running) {
        current_button = pirate_audio_hat_poll_button(&hat);
        if (current_button != PIRATE_AUDIO_BUTTON_NONE && current_button != last_reported_button) {
            const char *button_name = pirate_audio_button_name(current_button);
            double tone = pirate_audio_button_tone(current_button);

            draw_pirate_audio_screen(&hat, button_name);
            printf("Button pressed: %s\n", button_name);
            if (tone > 0.0) {
                demo_play_single_tone(audio_device, tone, 0.18);
            }
            last_reported_button = current_button;
        } else if (current_button == PIRATE_AUDIO_BUTTON_NONE) {
            last_reported_button = PIRATE_AUDIO_BUTTON_NONE;
        }

        sleep_for_ms(60);
    }

    pirate_audio_hat_fill_screen(&hat, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(&hat, 40, 112, "GOODBYE", PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLACK, 3);
    sleep_for_ms(200);
    pirate_audio_hat_close(&hat);
    return 0;
}

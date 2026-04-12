#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

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

static void choose_device_from_list(char *current_device, size_t current_device_size) {
    char device_names[50][64];
    int device_count = demo_list_playback_devices(device_names, 50);
    int choice;

    if (device_count == 0) {
        printf("No ALSA devices found.\n");
        return;
    }

    printf("\nSelect device number (1-%d, or 0 to cancel): ", device_count);
    fflush(stdout);

    if (scanf("%d", &choice) != 1) {
        while (getchar() != '\n') {
        }
        printf("Invalid input.\n");
        return;
    }

    while (getchar() != '\n') {
    }

    if (choice > 0 && choice <= device_count) {
        strncpy(current_device, device_names[choice - 1], current_device_size - 1);
        current_device[current_device_size - 1] = '\0';
        printf("Device successfully changed to: %s\n", current_device);
    }
}

static void print_unicode_demo(const char *device) {
    printf("\n=== Example: flite ===\n");
    printf("Current Audio Device: %s\n\n", device);
    printf("--- Unicode Font Display Test ---\n");
    printf("English:  Hello World\n");
    printf("Hindi:    नमस्ते दुनिया\n");
    printf("Japanese: こんにちは世界\n");
    printf("Russian:  Привет, мир\n");
    printf("Spanish:  ¡Hola Mundo!\n");
    printf("---------------------------------\n\n");
    printf("Menu:\n");
    printf("1 = Select Audio Device\n");
    printf("2 = Play Melody\n");
    printf("3 = Redraw Pirate Audio Screen\n");
    printf("4 = Quit\n");
    printf("Pirate Audio buttons A/B/X/Y update the HAT display and play tones.\n");
    printf("Enter choice then press Return: ");
    fflush(stdout);
}

static void draw_flite_screen(pirate_audio_hat_t *hat, const char *device, const char *message) {
    pirate_audio_hat_fill_screen(hat, PIRATE_AUDIO_COLOR_NAVY);
    pirate_audio_hat_fill_rect(hat, 8, 8, 224, 34, PIRATE_AUDIO_COLOR_BLUE);
    pirate_audio_hat_draw_text(hat, 20, 18, "FLITE DEMO", PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLUE, 3);

    pirate_audio_hat_fill_rect(hat, 16, 56, 208, 26, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 20, 64, "UNICODE + AUDIO", PIRATE_AUDIO_COLOR_YELLOW, PIRATE_AUDIO_COLOR_BLACK, 2);

    pirate_audio_hat_fill_rect(hat, 16, 96, 208, 24, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 20, 104, "DEVICE:", PIRATE_AUDIO_COLOR_CYAN, PIRATE_AUDIO_COLOR_BLACK, 2);
    pirate_audio_hat_draw_text(hat, 104, 104, device, PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLACK, 1);

    pirate_audio_hat_fill_rect(hat, 16, 136, 208, 28, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 20, 144, "LAST KEY:", PIRATE_AUDIO_COLOR_GREEN, PIRATE_AUDIO_COLOR_BLACK, 2);
    pirate_audio_hat_draw_text(hat, 20, 168, message, PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLACK, 2);

    pirate_audio_hat_fill_rect(hat, 16, 206, 208, 20, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(hat, 18, 212, "A B X Y = TONE + SCREEN", PIRATE_AUDIO_COLOR_ORANGE, PIRATE_AUDIO_COLOR_BLACK, 1);
}

int main(int argc, char **argv) {
    const char *spi_device = argc > 1 ? argv[1] : "/dev/spidev0.1";
    const char *gpiochip_device = argc > 2 ? argv[2] : "/dev/gpiochip0";
    pirate_audio_hat_t hat;
    pirate_audio_button_t last_reported_button = PIRATE_AUDIO_BUTTON_NONE;
    char device[64] = "default";
    char line[32];

    setlocale(LC_ALL, "");
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (pirate_audio_hat_init(&hat, spi_device, gpiochip_device) != 0) {
        fprintf(stderr, "Failed to initialize the Pirate Audio speaker HAT.\n");
        fprintf(stderr, "Try enabling the Pirate Audio profile before running this example.\n");
        return 1;
    }

    draw_flite_screen(&hat, device, "NONE");
    print_unicode_demo(device);

    while (keep_running) {
        pirate_audio_button_t current_button = pirate_audio_hat_poll_button(&hat);

        if (current_button != PIRATE_AUDIO_BUTTON_NONE && current_button != last_reported_button) {
            const char *button_name = pirate_audio_button_name(current_button);
            double tone = pirate_audio_button_tone(current_button);

            draw_flite_screen(&hat, device, button_name);
            printf("\nHat button pressed: %s\n", button_name);
            if (tone > 0.0) {
                demo_play_single_tone(device, tone, 0.18);
            }
            print_unicode_demo(device);
            last_reported_button = current_button;
        } else if (current_button == PIRATE_AUDIO_BUTTON_NONE) {
            last_reported_button = PIRATE_AUDIO_BUTTON_NONE;
        }

        {
            fd_set readfds;
            struct timeval timeout;
            int ready;

            FD_ZERO(&readfds);
            FD_SET(STDIN_FILENO, &readfds);
            timeout.tv_sec = 0;
            timeout.tv_usec = 120000;

            ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
            if (ready > 0 && FD_ISSET(STDIN_FILENO, &readfds)) {
                if (fgets(line, sizeof(line), stdin) == NULL) {
                    break;
                }

                switch (line[0]) {
                    case '1':
                        choose_device_from_list(device, sizeof(device));
                        draw_flite_screen(&hat, device, "NONE");
                        break;
                    case '2':
                        printf("Playing melody on '%s'\n", device);
                        demo_play_c_major_arpeggio(device);
                        break;
                    case '3':
                        draw_flite_screen(&hat, device, "NONE");
                        break;
                    case '4':
                        keep_running = 0;
                        continue;
                    default:
                        printf("Unknown option.\n");
                        break;
                }

                print_unicode_demo(device);
            }
        }

        sleep_for_ms(20);
    }

    pirate_audio_hat_fill_screen(&hat, PIRATE_AUDIO_COLOR_BLACK);
    pirate_audio_hat_draw_text(&hat, 58, 110, "BYE", PIRATE_AUDIO_COLOR_WHITE, PIRATE_AUDIO_COLOR_BLACK, 3);
    sleep_for_ms(200);
    pirate_audio_hat_close(&hat);
    return 0;
}

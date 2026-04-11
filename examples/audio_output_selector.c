#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "demo_audio.h"

typedef struct {
    const char *label;
    const char *alsa_device;
    const char *route_command;
} audio_output_profile_t;

static const audio_output_profile_t AUDIO_OUTPUT_PROFILES[] = {
    {"HDMI", "default", "amixer cset numid=3 2 >/tmp/audio-route.log 2>&1"},
    {"3.5mm Jack", "default", "amixer cset numid=3 1 >/tmp/audio-route.log 2>&1"},
    {"WM8960 I2S HAT", "default", NULL}
};

static void copy_string(char *dest, size_t dest_size, const char *src) {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
}

static void apply_route_command(const char *route_command) {
    int exit_code;

    if (route_command == NULL || route_command[0] == '\0') {
        return;
    }

    exit_code = system(route_command);
    if (exit_code != 0) {
        printf("Route command failed or is unavailable: %s\n", route_command);
        printf("If needed, install or load amixer/ALSA controls and retry.\n");
    }
}

static void select_profile(char *selected_label,
                           size_t selected_label_size,
                           char *selected_device,
                           size_t selected_device_size) {
    int choice;
    size_t profile_count = sizeof(AUDIO_OUTPUT_PROFILES) / sizeof(AUDIO_OUTPUT_PROFILES[0]);

    printf("\n=== Audio Output Profiles ===\n");
    for (size_t i = 0; i < profile_count; i++) {
        printf("%zu. %s\n", i + 1, AUDIO_OUTPUT_PROFILES[i].label);
    }
    printf("%zu. Custom ALSA Device Only\n", profile_count + 1);
    printf("Select profile: ");

    if (scanf("%d", &choice) != 1) {
        while (getchar() != '\n') {
        }
        printf("Invalid input.\n");
        return;
    }

    if (choice >= 1 && choice <= (int)profile_count) {
        const audio_output_profile_t *profile = &AUDIO_OUTPUT_PROFILES[choice - 1];

        copy_string(selected_label, selected_label_size, profile->label);
        copy_string(selected_device, selected_device_size, profile->alsa_device);
        apply_route_command(profile->route_command);
        printf("Selected profile: %s\n", selected_label);
        printf("ALSA device: %s\n", selected_device);
        return;
    }

    if (choice == (int)profile_count + 1) {
        char custom_device[64];

        printf("Enter ALSA device name: ");
        scanf("%63s", custom_device);
        copy_string(selected_label, selected_label_size, "Custom");
        copy_string(selected_device, selected_device_size, custom_device);
        printf("Selected custom ALSA device: %s\n", selected_device);
        return;
    }

    printf("Invalid selection.\n");
}

int main(void) {
    int choice;
    char selected_label[64] = "HDMI";
    char selected_device[64] = "default";

    apply_route_command(AUDIO_OUTPUT_PROFILES[0].route_command);

    while (1) {
        printf("\n=== Example: audio_output_selector ===\n");
        printf("Current Output Profile: %s\n", selected_label);
        printf("Current ALSA Device: %s\n", selected_device);
        printf("1. Select Audio Output Profile\n");
        printf("2. Play Test Melody\n");
        printf("3. Exit\n");
        printf("Select an option: ");

        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n') {
            }
            printf("Invalid input. Please enter a number.\n");
            continue;
        }

        switch (choice) {
            case 1:
                select_profile(selected_label, sizeof(selected_label), selected_device, sizeof(selected_device));
                break;
            case 2:
                printf("Playing on profile '%s' using ALSA device '%s'\n", selected_label, selected_device);
                demo_play_c_major_arpeggio(selected_device);
                break;
            case 3:
                printf("Exiting...\n");
                return 0;
            default:
                printf("Invalid choice. Please select 1, 2, or 3.\n");
                break;
        }
    }
}

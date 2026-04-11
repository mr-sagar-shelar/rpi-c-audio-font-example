#include <stdio.h>
#include <string.h>

#include "demo_audio.h"

static void choose_device_from_list(char *current_device, size_t current_device_size) {
    char device_names[50][64];
    int device_count = demo_list_playback_devices(device_names, 50);

    if (device_count == 0) {
        printf("No ALSA devices found.\n");
        return;
    }

    printf("\nSelect device number (1-%d, or 0 to cancel): ", device_count);

    int choice;
    if (scanf("%d", &choice) != 1) {
        while (getchar() != '\n') {
        }
        printf("Invalid input.\n");
        return;
    }

    if (choice > 0 && choice <= device_count) {
        strncpy(current_device, device_names[choice - 1], current_device_size - 1);
        current_device[current_device_size - 1] = '\0';
        printf("Device successfully changed to: %s\n", current_device);
    }
}

int main(void) {
    int choice;
    char device[64] = "default";

    while (1) {
        printf("\n=== Example: second ===\n");
        printf("Current Audio Device: %s\n", device);
        printf("1. Select Audio Device from List\n");
        printf("2. Play Melody\n");
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
                choose_device_from_list(device, sizeof(device));
                break;
            case 2:
                demo_play_c_major_arpeggio(device);
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

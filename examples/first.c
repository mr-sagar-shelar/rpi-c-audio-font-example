#include <stdio.h>

#include "demo_audio.h"

int main(void) {
    int choice;
    char device[64] = "default";

    while (1) {
        printf("\n=== Example: first ===\n");
        printf("Current Audio Device: %s\n", device);
        printf("1. Change Audio Device (e.g., default, hw:0,0, hw:1,0)\n");
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
                printf("Enter device name: ");
                scanf("%63s", device);
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

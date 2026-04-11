#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define TEA5767_I2C_ADDRESS 0x60
#define TEA5767_FREQ_MIN 87.5
#define TEA5767_FREQ_MAX 108.0

typedef struct {
    const char *name;
    double frequency_mhz;
} tea5767_preset_t;

static const tea5767_preset_t TEA5767_PRESETS[] = {
    {"AIR FM Gold", 100.1},
    {"Radio Mirchi", 98.3},
    {"Red FM", 93.5},
    {"Fever FM", 104.0},
    {"Big FM", 92.7}
};

static int tea5767_write_config(int fd, const unsigned char data[5]) {
    if (write(fd, data, 5) != 5) {
        fprintf(stderr, "Failed to write TEA5767 config: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int tea5767_read_status(int fd, unsigned char data[5]) {
    if (read(fd, data, 5) != 5) {
        fprintf(stderr, "Failed to read TEA5767 status: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

static int tea5767_set_frequency(int fd, double frequency_mhz, int muted) {
    unsigned char data[5];
    unsigned int pll_word;

    if (frequency_mhz < TEA5767_FREQ_MIN || frequency_mhz > TEA5767_FREQ_MAX) {
        fprintf(stderr, "Frequency %.1f MHz is out of range.\n", frequency_mhz);
        return -1;
    }

    pll_word = (unsigned int)lrint((4.0 * ((frequency_mhz * 1000000.0) + 225000.0)) / 32768.0);

    data[0] = (unsigned char)(((muted ? 0x80 : 0x00) | ((pll_word >> 8) & 0x3F)));
    data[1] = (unsigned char)(pll_word & 0xFF);
    data[2] = 0xB0;
    data[3] = 0x10;
    data[4] = 0x00;

    return tea5767_write_config(fd, data);
}

static double tea5767_decode_frequency(const unsigned char status[5]) {
    unsigned int pll_word = ((unsigned int)(status[0] & 0x3F) << 8) | status[1];
    return (((double)pll_word * 32768.0) / 4.0 - 225000.0) / 1000000.0;
}

static int tea5767_signal_level(const unsigned char status[5]) {
    return (status[3] >> 4) & 0x0F;
}

static int tea5767_is_stereo(const unsigned char status[5]) {
    return (status[2] & 0x80) != 0;
}

static void tea5767_show_status(int fd) {
    unsigned char status[5];

    if (tea5767_read_status(fd, status) != 0) {
        return;
    }

    printf("Current frequency: %.1f MHz\n", tea5767_decode_frequency(status));
    printf("Stereo: %s\n", tea5767_is_stereo(status) ? "yes" : "no");
    printf("Signal level: %d/15\n", tea5767_signal_level(status));
    printf("Ready flag: %s\n", (status[0] & 0x80) ? "ready" : "tuning");
}

static void tea5767_list_presets(void) {
    size_t preset_count = sizeof(TEA5767_PRESETS) / sizeof(TEA5767_PRESETS[0]);

    printf("\n=== Preset Stations ===\n");
    for (size_t i = 0; i < preset_count; i++) {
        printf("%zu. %s (%.1f MHz)\n", i + 1, TEA5767_PRESETS[i].name, TEA5767_PRESETS[i].frequency_mhz);
    }
}

int main(int argc, char **argv) {
    const char *i2c_device = argc > 1 ? argv[1] : "/dev/i2c-1";
    int fd;
    double current_frequency = 100.1;
    int muted = 0;

    fd = open(i2c_device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open I2C device %s: %s\n", i2c_device, strerror(errno));
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, TEA5767_I2C_ADDRESS) < 0) {
        fprintf(stderr, "Failed to select TEA5767 at 0x%02X: %s\n", TEA5767_I2C_ADDRESS, strerror(errno));
        close(fd);
        return 1;
    }

    if (tea5767_set_frequency(fd, current_frequency, muted) != 0) {
        close(fd);
        return 1;
    }

    printf("TEA5767 FM radio control on %s\n", i2c_device);
    printf("Radio audio is output from the module's LOUT/ROUT pins.\n");
    printf("Connect those pins to an external amplifier, powered speaker, or headphones.\n");

    while (1) {
        int choice;

        printf("\n=== TEA5767 FM Radio Menu ===\n");
        printf("Current tuned frequency: %.1f MHz\n", current_frequency);
        printf("Muted: %s\n", muted ? "yes" : "no");
        printf("1. Show tuner status\n");
        printf("2. List and select preset station\n");
        printf("3. Enter frequency manually\n");
        printf("4. Tune down by 0.1 MHz\n");
        printf("5. Tune up by 0.1 MHz\n");
        printf("6. Toggle mute\n");
        printf("7. Exit\n");
        printf("Select an option: ");

        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n') {
            }
            printf("Invalid input.\n");
            continue;
        }

        switch (choice) {
            case 1:
                tea5767_show_status(fd);
                break;
            case 2: {
                int preset_choice;
                size_t preset_count = sizeof(TEA5767_PRESETS) / sizeof(TEA5767_PRESETS[0]);

                tea5767_list_presets();
                printf("Select preset number: ");
                if (scanf("%d", &preset_choice) != 1) {
                    while (getchar() != '\n') {
                    }
                    printf("Invalid input.\n");
                    break;
                }
                if (preset_choice < 1 || preset_choice > (int)preset_count) {
                    printf("Invalid preset.\n");
                    break;
                }

                current_frequency = TEA5767_PRESETS[preset_choice - 1].frequency_mhz;
                tea5767_set_frequency(fd, current_frequency, muted);
                printf("Tuned to %s at %.1f MHz\n",
                       TEA5767_PRESETS[preset_choice - 1].name,
                       current_frequency);
                break;
            }
            case 3: {
                double new_frequency;

                printf("Enter frequency in MHz (%.1f - %.1f): ", TEA5767_FREQ_MIN, TEA5767_FREQ_MAX);
                if (scanf("%lf", &new_frequency) != 1) {
                    while (getchar() != '\n') {
                    }
                    printf("Invalid input.\n");
                    break;
                }

                if (tea5767_set_frequency(fd, new_frequency, muted) == 0) {
                    current_frequency = new_frequency;
                    printf("Tuned to %.1f MHz\n", current_frequency);
                }
                break;
            }
            case 4:
                current_frequency -= 0.1;
                if (current_frequency < TEA5767_FREQ_MIN) {
                    current_frequency = TEA5767_FREQ_MIN;
                }
                tea5767_set_frequency(fd, current_frequency, muted);
                printf("Tuned to %.1f MHz\n", current_frequency);
                break;
            case 5:
                current_frequency += 0.1;
                if (current_frequency > TEA5767_FREQ_MAX) {
                    current_frequency = TEA5767_FREQ_MAX;
                }
                tea5767_set_frequency(fd, current_frequency, muted);
                printf("Tuned to %.1f MHz\n", current_frequency);
                break;
            case 6:
                muted = !muted;
                tea5767_set_frequency(fd, current_frequency, muted);
                printf("Mute is now %s\n", muted ? "enabled" : "disabled");
                break;
            case 7:
                close(fd);
                return 0;
            default:
                printf("Invalid choice.\n");
                break;
        }
    }
}

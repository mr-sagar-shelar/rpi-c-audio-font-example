#include <errno.h>
#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#define RV3028_I2C_ADDRESS 0x52

#define RV3028_REG_SECONDS 0x00
#define RV3028_REG_MINUTES 0x01
#define RV3028_REG_HOURS 0x02
#define RV3028_REG_WEEKDAY 0x03
#define RV3028_REG_DATE 0x04
#define RV3028_REG_MONTH 0x05
#define RV3028_REG_YEAR 0x06

#define RV3028_REG_STATUS 0x0E
#define RV3028_REG_CONTROL1 0x0F
#define RV3028_REG_CONTROL2 0x10

#define RV3028_REG_EEPROM_ADDR 0x25
#define RV3028_REG_EEPROM_DATA 0x26
#define RV3028_REG_EEPROM_CMD 0x27

#define RV3028_STATUS_EEBUSY 0x80
#define RV3028_STATUS_AF 0x04

#define RV3028_CONTROL1_WADA 0x20
#define RV3028_CONTROL2_EERD 0x08
#define RV3028_CONTROL2_AIE 0x02

#define RV3028_EEPROM_CMD_WRITE 0x21
#define RV3028_EEPROM_CMD_READ 0x22

#define RV3028_MAX_ALARMS 5
#define RV3028_ALARM_MAGIC 0xA5
#define RV3028_ALARM_EEPROM_BASE 0x00
#define RV3028_ALARM_SLOT_SIZE 7

typedef struct {
    uint8_t valid;
    uint8_t enabled;
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
} rv3028_alarm_t;

typedef struct {
    struct tm now;
    rv3028_alarm_t alarms[RV3028_MAX_ALARMS];
    int last_triggered_index;
} rv3028_state_t;

static volatile sig_atomic_t keep_running = 1;

static void handle_signal(int signal_number) {
    (void)signal_number;
    keep_running = 0;
}

static int write_register(int fd, uint8_t reg, uint8_t value) {
    uint8_t buffer[2];

    buffer[0] = reg;
    buffer[1] = value;

    if (write(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        fprintf(stderr, "Failed to write register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    return 0;
}

static int read_register(int fd, uint8_t reg, uint8_t *value) {
    if (write(fd, &reg, 1) != 1) {
        fprintf(stderr, "Failed to select register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    if (read(fd, value, 1) != 1) {
        fprintf(stderr, "Failed to read register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    return 0;
}

static int read_registers(int fd, uint8_t start_reg, uint8_t *buffer, size_t length) {
    if (write(fd, &start_reg, 1) != 1) {
        fprintf(stderr, "Failed to select register block 0x%02X: %s\n", start_reg, strerror(errno));
        return -1;
    }

    if (read(fd, buffer, length) != (ssize_t)length) {
        fprintf(stderr, "Failed to read register block 0x%02X: %s\n", start_reg, strerror(errno));
        return -1;
    }

    return 0;
}

static uint8_t to_bcd(int value) {
    return (uint8_t)(((value / 10) << 4) | (value % 10));
}

static int from_bcd(uint8_t value) {
    return ((value >> 4) * 10) + (value & 0x0F);
}

static int rv3028_wait_eeprom_idle(int fd) {
    uint8_t status;

    for (int i = 0; i < 50; i++) {
        if (read_register(fd, RV3028_REG_STATUS, &status) != 0) {
            return -1;
        }
        if ((status & RV3028_STATUS_EEBUSY) == 0) {
            return 0;
        }
        usleep(2000);
    }

    fprintf(stderr, "EEPROM stayed busy too long.\n");
    return -1;
}

static int rv3028_set_eerd(int fd, int enabled) {
    uint8_t control2;

    if (read_register(fd, RV3028_REG_CONTROL2, &control2) != 0) {
        return -1;
    }

    if (enabled) {
        control2 |= RV3028_CONTROL2_EERD;
    } else {
        control2 &= (uint8_t)~RV3028_CONTROL2_EERD;
    }

    return write_register(fd, RV3028_REG_CONTROL2, control2);
}

static int rv3028_eeprom_read_byte(int fd, uint8_t eeprom_addr, uint8_t *value) {
    if (rv3028_set_eerd(fd, 1) != 0) {
        return -1;
    }
    if (rv3028_wait_eeprom_idle(fd) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    if (write_register(fd, RV3028_REG_EEPROM_ADDR, eeprom_addr) != 0 ||
        write_register(fd, RV3028_REG_EEPROM_CMD, RV3028_EEPROM_CMD_READ) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    usleep(2000);
    if (rv3028_wait_eeprom_idle(fd) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    if (read_register(fd, RV3028_REG_EEPROM_DATA, value) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    return rv3028_set_eerd(fd, 0);
}

static int rv3028_eeprom_write_byte(int fd, uint8_t eeprom_addr, uint8_t value) {
    if (rv3028_set_eerd(fd, 1) != 0) {
        return -1;
    }
    if (rv3028_wait_eeprom_idle(fd) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    if (write_register(fd, RV3028_REG_EEPROM_ADDR, eeprom_addr) != 0 ||
        write_register(fd, RV3028_REG_EEPROM_DATA, value) != 0 ||
        write_register(fd, RV3028_REG_EEPROM_CMD, RV3028_EEPROM_CMD_WRITE) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    usleep(12000);
    if (rv3028_wait_eeprom_idle(fd) != 0) {
        rv3028_set_eerd(fd, 0);
        return -1;
    }
    return rv3028_set_eerd(fd, 0);
}

static int rv3028_load_alarms(int fd, rv3028_alarm_t alarms[RV3028_MAX_ALARMS]) {
    uint8_t magic = 0;

    if (rv3028_eeprom_read_byte(fd, RV3028_ALARM_EEPROM_BASE, &magic) != 0) {
        return -1;
    }

    if (magic != RV3028_ALARM_MAGIC) {
        memset(alarms, 0, sizeof(rv3028_alarm_t) * RV3028_MAX_ALARMS);
        if (rv3028_eeprom_write_byte(fd, RV3028_ALARM_EEPROM_BASE, RV3028_ALARM_MAGIC) != 0) {
            return -1;
        }
        return 0;
    }

    for (int i = 0; i < RV3028_MAX_ALARMS; i++) {
        uint8_t base = RV3028_ALARM_EEPROM_BASE + 1 + (i * RV3028_ALARM_SLOT_SIZE);

        if (rv3028_eeprom_read_byte(fd, base + 0, &alarms[i].valid) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 1, &alarms[i].enabled) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 2, &alarms[i].year) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 3, &alarms[i].month) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 4, &alarms[i].day) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 5, &alarms[i].hour) != 0 ||
            rv3028_eeprom_read_byte(fd, base + 6, &alarms[i].minute) != 0) {
            return -1;
        }
    }

    return 0;
}

static int rv3028_save_alarm(int fd, int index, const rv3028_alarm_t *alarm) {
    uint8_t base = RV3028_ALARM_EEPROM_BASE + 1 + (index * RV3028_ALARM_SLOT_SIZE);

    if (rv3028_eeprom_write_byte(fd, base + 0, alarm->valid) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 1, alarm->enabled) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 2, alarm->year) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 3, alarm->month) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 4, alarm->day) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 5, alarm->hour) != 0 ||
        rv3028_eeprom_write_byte(fd, base + 6, alarm->minute) != 0) {
        return -1;
    }

    return 0;
}

static int rv3028_read_time(int fd, struct tm *time_info) {
    uint8_t raw[7];

    if (read_registers(fd, RV3028_REG_SECONDS, raw, sizeof(raw)) != 0) {
        return -1;
    }

    memset(time_info, 0, sizeof(*time_info));
    time_info->tm_sec = from_bcd(raw[0] & 0x7F);
    time_info->tm_min = from_bcd(raw[1] & 0x7F);
    time_info->tm_hour = from_bcd(raw[2] & 0x3F);
    time_info->tm_wday = raw[3] & 0x07;
    time_info->tm_mday = from_bcd(raw[4] & 0x3F);
    time_info->tm_mon = from_bcd(raw[5] & 0x1F) - 1;
    time_info->tm_year = 100 + from_bcd(raw[6]);

    return 0;
}

static int rv3028_set_time(int fd, const struct tm *time_info) {
    uint8_t buffer[8];

    buffer[0] = RV3028_REG_SECONDS;
    buffer[1] = to_bcd(time_info->tm_sec);
    buffer[2] = to_bcd(time_info->tm_min);
    buffer[3] = to_bcd(time_info->tm_hour);
    buffer[4] = (uint8_t)(time_info->tm_wday & 0x07);
    buffer[5] = to_bcd(time_info->tm_mday);
    buffer[6] = to_bcd(time_info->tm_mon + 1);
    buffer[7] = to_bcd((time_info->tm_year + 1900) % 100);

    if (write(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        fprintf(stderr, "Failed to write RTC time: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

static void rv3028_print_time(const struct tm *time_info) {
    printf("%04d-%02d-%02d %02d:%02d:%02d\n",
           time_info->tm_year + 1900,
           time_info->tm_mon + 1,
           time_info->tm_mday,
           time_info->tm_hour,
           time_info->tm_min,
           time_info->tm_sec);
}

static void rv3028_list_alarms(const rv3028_state_t *state) {
    printf("\n=== Saved Alarms ===\n");
    for (int i = 0; i < RV3028_MAX_ALARMS; i++) {
        const rv3028_alarm_t *alarm = &state->alarms[i];

        if (!alarm->valid) {
            printf("%d. <empty>\n", i + 1);
            continue;
        }

        printf("%d. %s %02u-%02u-%02u %02u:%02u\n",
               i + 1,
               alarm->enabled ? "[enabled]" : "[disabled]",
               (unsigned)alarm->year,
               (unsigned)alarm->month,
               (unsigned)alarm->day,
               (unsigned)alarm->hour,
               (unsigned)alarm->minute);
    }

    if (state->last_triggered_index >= 0) {
        const rv3028_alarm_t *alarm = &state->alarms[state->last_triggered_index];
        printf("Last triggered alarm: #%d at %02u-%02u-%02u %02u:%02u\n",
               state->last_triggered_index + 1,
               (unsigned)alarm->year,
               (unsigned)alarm->month,
               (unsigned)alarm->day,
               (unsigned)alarm->hour,
               (unsigned)alarm->minute);
    }
}

static int rv3028_alarm_matches_now(const rv3028_alarm_t *alarm, const struct tm *now) {
    return alarm->valid &&
           alarm->enabled &&
           alarm->year == (uint8_t)((now->tm_year + 1900) % 100) &&
           alarm->month == (uint8_t)(now->tm_mon + 1) &&
           alarm->day == (uint8_t)now->tm_mday &&
           alarm->hour == (uint8_t)now->tm_hour &&
           alarm->minute == (uint8_t)now->tm_min;
}

static void rv3028_check_alarm_triggers(rv3028_state_t *state) {
    for (int i = 0; i < RV3028_MAX_ALARMS; i++) {
        if (rv3028_alarm_matches_now(&state->alarms[i], &state->now)) {
            if (state->last_triggered_index != i) {
                printf("\n*** Alarm #%d triggered at ", i + 1);
                rv3028_print_time(&state->now);
                state->last_triggered_index = i;
            }
            return;
        }
    }

    if (state->last_triggered_index >= 0) {
        const rv3028_alarm_t *last_alarm = &state->alarms[state->last_triggered_index];
        if (!rv3028_alarm_matches_now(last_alarm, &state->now)) {
            state->last_triggered_index = -1;
        }
    }
}

static void rv3028_show_alarm_message(const rv3028_state_t *state) {
    if (state->last_triggered_index < 0) {
        printf("No active alarm trigger.\n");
        return;
    }

    printf("Alarm #%d is active in the current minute.\n", state->last_triggered_index + 1);
}

static int prompt_alarm_slot(void) {
    int slot;

    printf("Enter alarm slot (1-%d): ", RV3028_MAX_ALARMS);
    if (scanf("%d", &slot) != 1) {
        while (getchar() != '\n') {
        }
        return -1;
    }

    if (slot < 1 || slot > RV3028_MAX_ALARMS) {
        return -1;
    }

    return slot - 1;
}

static int prompt_alarm_datetime(rv3028_alarm_t *alarm) {
    int year, month, day, hour, minute;

    printf("Enter alarm time as YY MM DD HH MM: ");
    if (scanf("%d %d %d %d %d", &year, &month, &day, &hour, &minute) != 5) {
        while (getchar() != '\n') {
        }
        return -1;
    }

    if (year < 0 || year > 99 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59) {
        return -1;
    }

    alarm->valid = 1;
    alarm->enabled = 1;
    alarm->year = (uint8_t)year;
    alarm->month = (uint8_t)month;
    alarm->day = (uint8_t)day;
    alarm->hour = (uint8_t)hour;
    alarm->minute = (uint8_t)minute;
    return 0;
}

int main(int argc, char **argv) {
    const char *i2c_device = argc > 1 ? argv[1] : "/dev/i2c-1";
    int fd;
    rv3028_state_t state;

    memset(&state, 0, sizeof(state));
    state.last_triggered_index = -1;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    fd = open(i2c_device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open I2C device %s: %s\n", i2c_device, strerror(errno));
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, RV3028_I2C_ADDRESS) < 0) {
        fprintf(stderr, "Failed to select RV3028 at 0x%02X: %s\n", RV3028_I2C_ADDRESS, strerror(errno));
        close(fd);
        return 1;
    }

    if (rv3028_load_alarms(fd, state.alarms) != 0) {
        close(fd);
        return 1;
    }

    printf("RV3028 RTC menu using %s (I2C 0x%02X)\n", i2c_device, RV3028_I2C_ADDRESS);

    while (keep_running) {
        int choice;

        if (rv3028_read_time(fd, &state.now) != 0) {
            break;
        }
        rv3028_check_alarm_triggers(&state);

        printf("\n=== RV3028 RTC Menu ===\n");
        printf("Current RTC time: ");
        rv3028_print_time(&state.now);
        rv3028_show_alarm_message(&state);
        printf("1. Show RTC time\n");
        printf("2. Set RTC time\n");
        printf("3. List alarms\n");
        printf("4. Add alarm\n");
        printf("5. Update alarm\n");
        printf("6. Remove alarm\n");
        printf("7. Enable/Disable alarm\n");
        printf("8. Refresh\n");
        printf("9. Exit\n");
        printf("Select an option: ");

        if (scanf("%d", &choice) != 1) {
            while (getchar() != '\n') {
            }
            printf("Invalid input.\n");
            continue;
        }

        switch (choice) {
            case 1:
                printf("RTC time: ");
                rv3028_print_time(&state.now);
                break;
            case 2: {
                struct tm new_time = {0};
                int year, month, day, hour, minute, second;

                printf("Enter new time as YYYY MM DD HH MM SS: ");
                if (scanf("%d %d %d %d %d %d", &year, &month, &day, &hour, &minute, &second) != 6) {
                    while (getchar() != '\n') {
                    }
                    printf("Invalid input.\n");
                    break;
                }

                new_time.tm_year = year - 1900;
                new_time.tm_mon = month - 1;
                new_time.tm_mday = day;
                new_time.tm_hour = hour;
                new_time.tm_min = minute;
                new_time.tm_sec = second;
                new_time.tm_isdst = -1;
                mktime(&new_time);

                if (rv3028_set_time(fd, &new_time) == 0) {
                    printf("RTC time updated.\n");
                }
                break;
            }
            case 3:
                rv3028_list_alarms(&state);
                break;
            case 4:
            case 5: {
                int slot = prompt_alarm_slot();
                rv3028_alarm_t alarm;

                if (slot < 0) {
                    printf("Invalid slot.\n");
                    break;
                }

                alarm = state.alarms[slot];
                if (prompt_alarm_datetime(&alarm) != 0) {
                    printf("Invalid alarm time.\n");
                    break;
                }

                state.alarms[slot] = alarm;
                if (rv3028_save_alarm(fd, slot, &state.alarms[slot]) == 0) {
                    printf("Alarm #%d saved.\n", slot + 1);
                }
                break;
            }
            case 6: {
                int slot = prompt_alarm_slot();
                rv3028_alarm_t empty_alarm = {0};

                if (slot < 0) {
                    printf("Invalid slot.\n");
                    break;
                }

                state.alarms[slot] = empty_alarm;
                if (rv3028_save_alarm(fd, slot, &state.alarms[slot]) == 0) {
                    printf("Alarm #%d removed.\n", slot + 1);
                }
                break;
            }
            case 7: {
                int slot = prompt_alarm_slot();

                if (slot < 0 || !state.alarms[slot].valid) {
                    printf("Invalid slot.\n");
                    break;
                }

                state.alarms[slot].enabled = (uint8_t)!state.alarms[slot].enabled;
                if (rv3028_save_alarm(fd, slot, &state.alarms[slot]) == 0) {
                    printf("Alarm #%d %s.\n", slot + 1, state.alarms[slot].enabled ? "enabled" : "disabled");
                }
                break;
            }
            case 8:
                break;
            case 9:
                keep_running = 0;
                break;
            default:
                printf("Invalid choice.\n");
                break;
        }
    }

    close(fd);
    return 0;
}

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

#define INA219_I2C_ADDRESS 0x43
#define INA219_REG_BUS_VOLTAGE 0x02
#define INA219_REG_CURRENT 0x04
#define INA219_REG_CALIBRATION 0x05

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

static int write_register16(int fd, uint8_t reg, uint16_t value) {
    uint8_t buffer[3];

    buffer[0] = reg;
    buffer[1] = (uint8_t)((value >> 8) & 0xFF);
    buffer[2] = (uint8_t)(value & 0xFF);

    if (write(fd, buffer, sizeof(buffer)) != (ssize_t)sizeof(buffer)) {
        fprintf(stderr, "Failed to write INA219 register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    return 0;
}

static int read_register16(int fd, uint8_t reg, uint16_t *value) {
    uint8_t raw[2];

    if (write(fd, &reg, 1) != 1) {
        fprintf(stderr, "Failed to select INA219 register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    if (read(fd, raw, sizeof(raw)) != (ssize_t)sizeof(raw)) {
        fprintf(stderr, "Failed to read INA219 register 0x%02X: %s\n", reg, strerror(errno));
        return -1;
    }

    *value = (uint16_t)((raw[0] << 8) | raw[1]);
    return 0;
}

static int configure_ina219(int fd) {
    return write_register16(fd, INA219_REG_CALIBRATION, 4096);
}

static double read_bus_voltage_volts(int fd) {
    uint16_t raw = 0;

    if (read_register16(fd, INA219_REG_BUS_VOLTAGE, &raw) != 0) {
        return -1.0;
    }

    raw >>= 3;
    return (double)raw * 0.004;
}

static double read_current_milliamps(int fd) {
    uint16_t raw = 0;
    int16_t signed_raw;

    if (read_register16(fd, INA219_REG_CURRENT, &raw) != 0) {
        return 0.0;
    }

    signed_raw = (int16_t)raw;
    return (double)signed_raw * 0.1;
}

static double estimate_battery_percent(double volts) {
    double min_v = 3.2;
    double max_v = 4.2;
    double percent = ((volts - min_v) / (max_v - min_v)) * 100.0;

    if (percent < 0.0) {
        percent = 0.0;
    }
    if (percent > 100.0) {
        percent = 100.0;
    }

    return percent;
}

static const char *charging_status(double current_ma) {
    if (current_ma > 20.0) {
        return "CHARGING";
    }
    if (current_ma < -20.0) {
        return "DISCHARGING";
    }
    return "IDLE";
}

int main(int argc, char **argv) {
    const char *i2c_device = argc > 1 ? argv[1] : "/dev/i2c-1";
    long interval_ms = argc > 2 ? strtol(argv[2], NULL, 10) : 1200;
    int fd;

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    if (interval_ms < 200) {
        interval_ms = 200;
    }

    fd = open(i2c_device, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Failed to open I2C device %s: %s\n", i2c_device, strerror(errno));
        return 1;
    }

    if (ioctl(fd, I2C_SLAVE, INA219_I2C_ADDRESS) < 0) {
        fprintf(stderr, "Failed to select INA219 device at 0x%02X: %s\n", INA219_I2C_ADDRESS, strerror(errno));
        close(fd);
        return 1;
    }

    if (configure_ina219(fd) != 0) {
        close(fd);
        return 1;
    }

    printf("UPS HAT C monitor running on %s (INA219 @ 0x%02X)\n", i2c_device, INA219_I2C_ADDRESS);
    printf("Press Ctrl+C to stop.\n");

    while (keep_running) {
        double bus_voltage = read_bus_voltage_volts(fd);
        double current_ma = read_current_milliamps(fd);
        double percent;

        if (bus_voltage < 0.0) {
            fprintf(stderr, "Failed to read UPS telemetry.\n");
            break;
        }

        percent = estimate_battery_percent(bus_voltage);
        printf("Battery: %.2f V | %.0f%% | Current: %.1f mA | Status: %s\n",
               bus_voltage,
               percent,
               current_ma,
               charging_status(current_ma));

        sleep_for_ms(interval_ms);
    }

    close(fd);
    return 0;
}

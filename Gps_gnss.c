#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include "gps_gnss.h"
#define GPS_I2C_ADDR 0x42
#define BUFFER_SIZE 256

// ---------- GPS I2C Functions ----------

int gps_i2c_open(const char* i2c_bus) {
    int fd = open(i2c_bus, O_RDWR);
    if (fd < 0) {
        perror("gps_i2c_open: Failed to open I2C bus");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, GPS_I2C_ADDR) < 0) {
        perror("gps_i2c_open: Failed to set slave address");
        close(fd);
        return -1;
    }
    printf("Successfully opened GPS I2C address\n");
    return fd;
}

int gps_i2c_init(const char* i2c_bus) {
    return gps_i2c_open(i2c_bus);
}

void gps_i2c_close(int fd) {
    if (fd >= 0) {
        if (close(fd) < 0) {
            perror("gps_i2c_close: Error closing fd");
        } else {
            printf("Closed GPS I2C file descriptor\n");
        }
    }
}

// ---------- GPS Parsing Helpers ----------

double convert_to_decimal(const char* raw, const char* dir) {
    if (!raw || !dir) return 0.0;
    double value = atof(raw);
    int degrees = (int)(value / 100);
    double minutes = value - (degrees * 100);
    double decimal = degrees + minutes / 60.0;

    if (dir[0] == 'S' || dir[0] == 'W')
        decimal *= -1.0;

    return decimal;
}

int gps_read_line(int fd, char* buffer, int max_len) {
    if (!buffer || max_len <= 0) return -1;

    int pos = 0;
    char byte;
    while (pos < max_len - 1) {
        int ret = read(fd, &byte, 1);
        if (ret == 0) {
            fprintf(stderr, "gps_read_line: EOF reached\n");
            break;
        } else if (ret < 0) {
            perror("gps_read_line: read error");
            break;
        }

        buffer[pos++] = byte;
        if (byte == '\n') break;
    }

    buffer[pos] = '\0';
    return pos > 0 ? pos : -1;
}

int gps_get_location(int fd, double* latitude, double* longitude) {
    char line[BUFFER_SIZE];
    while (1) {
        int len = gps_read_line(fd, line, sizeof(line));
        if (len <= 0 || line[0] != '$') continue;

        // Look for GGA sentence (or you can use RMC)
        if (strstr(line, "$GPGGA") == line || strstr(line, "$GNGGA") == line) {
            char* tokens[15] = {0};
            char* tok = strtok(line, ",");
            int i = 0;
            while (tok && i < 15) {
                tokens[i++] = tok;
                tok = strtok(NULL, ",");
            }
            if (i < 6 || !tokens[2] || !tokens[4]) continue;

            *latitude  = convert_to_decimal(tokens[2], tokens[3]);
            *longitude = convert_to_decimal(tokens[4], tokens[5]);
            return 0; // success
        }
    }
    return -1; // failed to get valid sentence
}



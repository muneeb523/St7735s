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

 int gps_i2c_open(const char* i2c_bus) {
    int fd = open(i2c_bus, O_RDWR);
    if (fd < 0) return -1;
    if (ioctl(fd, I2C_SLAVE, GPS_I2C_ADDR) < 0) {
        close(fd);
        return -1;
    }
    printf("Sucessfull in opening the address location\n");
    return fd;
}

int gps_i2c_init(const char* i2c_bus) {
    return gps_i2c_open(i2c_bus);
}

void gps_i2c_close(int fd) {
    printf("Closing fd: %d\n", fd);
    if (fd >= 0) {
        if (close(fd) < 0) {
            perror("Error closing fd");
        }
    }
}

int gps_read_line(int fd, char* buffer, int max_len) {
    int pos = 0;
    char byte;
    while (pos < max_len - 1) {
        int ret = read(fd, &byte, 1);
        if (ret == 0) {
            // EOF: treat as error
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


// Convert NMEA lat/lon format to decimal
double convert_to_decimal(const char* nmea_coord, const char* direction) {
    if (!nmea_coord || !direction || direction[0] == '\0') return 0.0;

    double raw = atof(nmea_coord);
    int degrees = (int)(raw / 100);
    double minutes = raw - (degrees * 100);
    double decimal = degrees + (minutes / 60.0);

    if (direction[0] == 'S' || direction[0] == 'W') decimal *= -1;

    return decimal;
}

int gps_get_location(int fd, double* latitude, double* longitude) {
    char line[BUFFER_SIZE];
    while (1) {
        int len = gps_read_line(fd, line, sizeof(line));
        if (len <= 0 || line[0] != '$') continue;

        if (strstr(line, "$GPGGA") == line || strstr(line, "$GNGGA") == line) {
            char* tokens[15] = {0};
            char* tok = strtok(line, ",");
            int i = 0;
            while (tok && i < 15) {
                tokens[i++] = tok;
                tok = strtok(NULL, ",");
            }

            if (i >= 6 && tokens[2] && tokens[3] && tokens[4] && tokens[5]) {
                *latitude  = convert_to_decimal(tokens[2], tokens[3]);
                *longitude = convert_to_decimal(tokens[4], tokens[5]);
                return 0;
            }
        }
    }
    return -1;
}
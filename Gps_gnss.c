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
#define BQ25792_I2C_ADDR 0x6B
#define I2C_DEVICE "/dev/i2c-3"

// Register addresses

// Register addresses
#define REG_INPUT_CURRENT_LIMIT 0x00 // Input current limit register (50 mA/bit)
#define REG_VSYS_REG_1 0x01          // VSYS regulation voltage (lower 8 bits)
#define REG_VSYS_REG_2 0x02          // VSYS regulation voltage (upper 2 bits)
#define REG_CHARGE_CURRENT 0x03      // Charge current register (100 mA/bit for 2S)
#define REG_CHARGE_OPTION_0 0x04     // Charge option, including SHIP_MODE (bit 7)
#define REG_MIN_VSYS 0x05            // Minimum system voltage (3.0 V + 100 mV/bit)
#define REG_CHARGE_STATUS 0x20       // Charge status bits
#define REG_CHARGE_FAULT 0x21        // Fault indicator bits
#define REG_CHARGE_FLAGS 0x22        // Additional charger flags

// Charger states
enum ChargerState
{
    CHARGING = 1,
    NOT_CHARGING = 0,
    CHARGING_ERROR = -1,
    CHARGING_FAULT = -2
};

// ---------- GPS I2C Functions ----------

int gps_i2c_open(const char *i2c_bus)
{
    int fd = open(i2c_bus, O_RDWR);
    if (fd < 0)
    {
        perror("gps_i2c_open: Failed to open I2C bus");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, GPS_I2C_ADDR) < 0)
    {
        perror("gps_i2c_open: Failed to set slave address");
        close(fd);
        return -1;
    }
    printf("Successfully opened GPS I2C address\n");
    return fd;
}

int gps_i2c_init(const char *i2c_bus)
{
    return gps_i2c_open(i2c_bus);
}

void gps_i2c_close(int fd)
{
    if (fd >= 0)
    {
        if (close(fd) < 0)
        {
            perror("gps_i2c_close: Error closing fd");
        }
        else
        {
            printf("Closed GPS I2C file descriptor\n");
        }
    }
}

// ---------- GPS Parsing Helpers ----------

double convert_to_decimal(const char *raw, const char *dir)
{
    if (!raw || !dir)
        return 0.0;
    double value = atof(raw);
    int degrees = (int)(value / 100);
    double minutes = value - (degrees * 100);
    double decimal = degrees + minutes / 60.0;

    if (dir[0] == 'S' || dir[0] == 'W')
        decimal *= -1.0;

    return decimal;
}

int gps_read_line(int fd, char *buffer, int max_len)
{
    if (!buffer || max_len <= 0)
        return -1;

    int pos = 0;
    char byte;
    while (pos < max_len - 1)
    {
        int ret = read(fd, &byte, 1);
        if (ret == 0)
        {
            fprintf(stderr, "gps_read_line: EOF reached\n");
            break;
        }
        else if (ret < 0)
        {
            perror("gps_read_line: read error");
            break;
        }

        buffer[pos++] = byte;
        if (byte == '\n')
            break;
    }

    buffer[pos] = '\0';
    return pos > 0 ? pos : -1;
}

int gps_get_location(int fd, double *latitude, double *longitude)
{
    char line[BUFFER_SIZE];
    while (1)
    {
        int len = gps_read_line(fd, line, sizeof(line));
        if (len <= 0 || line[0] != '$')
            continue;

        // Look for GGA sentence (or you can use RMC)
        if (strstr(line, "$GPGGA") == line || strstr(line, "$GNGGA") == line)
        {
            char *tokens[15] = {0};
            char *tok = strtok(line, ",");
            int i = 0;
            while (tok && i < 15)
            {
                tokens[i++] = tok;
                tok = strtok(NULL, ",");
            }
            if (i < 6 || !tokens[2] || !tokens[4])
                continue;

            *latitude = convert_to_decimal(tokens[2], tokens[3]);
            *longitude = convert_to_decimal(tokens[4], tokens[5]);
            return 0; // success
        }
    }
    return -1; // failed to get valid sentence
}

int bq25792_open()
{
    int fd = open(I2C_DEVICE, O_RDWR);
    if (fd < 0)
    {
        perror("[BQ25792] Failed to open I2C bus");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, BQ25792_I2C_ADDR) < 0)
    {
        perror("[BQ25792] Failed to set I2C slave address");
        close(fd);
        return -1;
    }
    return fd;
}

int bq25792_read_reg(int fd, uint8_t reg, uint8_t *val)
{
    if (write(fd, &reg, 1) != 1)
    {
        perror("[BQ25792] I2C write failed");
        return -1;
    }
    if (read(fd, val, 1) != 1)
    {
        perror("[BQ25792] I2C read failed");
        return -1;
    }
    return 0;
}

int bq25792_write_reg(int fd, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = {reg, val};
    if (write(fd, buf, 2) != 2)
    {
        perror("[BQ25792] I2C write failed");
        return -1;
    }
    return 0;
}

int bq25792_get_charging_status()
{
    int fd = bq25792_open();
    if (fd < 0)
        return CHARGING_ERROR;

    uint8_t status = 0, fault = 0;
    if (bq25792_read_reg(fd, REG_CHARGE_STATUS, &status) < 0 ||
        bq25792_read_reg(fd, REG_CHARGE_FAULT, &fault) < 0)
    {
        close(fd);
        return CHARGING_ERROR;
    }

    close(fd);

    if (fault != 0)
        return CHARGING_FAULT;

    uint8_t chrg_stat = (status >> 1) & 0x07; // Bits 3:1
    return (chrg_stat != 0) ? CHARGING : NOT_CHARGING;
}

int bq25792_get_full_status()
{
    int fd = bq25792_open();
    if (fd < 0)
        return CHARGING_ERROR;

    uint8_t s0, s1, s2;
    if (bq25792_read_reg(fd, REG_CHARGE_STATUS, &s0) < 0 ||
        bq25792_read_reg(fd, REG_CHARGE_FAULT, &s1) < 0 ||
        bq25792_read_reg(fd, REG_CHARGE_FLAGS, &s2) < 0)
    {
        close(fd);
        return CHARGING_ERROR;
    }

    printf("[BQ25792] Status0=0x%02X, Fault=0x%02X, Flags=0x%02X\n", s0, s1, s2);
    close(fd);
    return 0;
}

int bq25792_enter_ship_mode()
{
    int fd = bq25792_open();
    if (fd < 0)
        return -1;

    uint8_t val = 0;
    if (bq25792_read_reg(fd, REG_CHARGE_OPTION_0, &val) < 0)
    {
        close(fd);
        return -1;
    }

    val |= (1 << 7); // Set SHIP_MODE bit
    int ret =  (fd, REG_CHARGE_OPTION_0, val);
    close(fd);

    if (ret == 0)
        printf("[BQ25792] Entered SHIP MODE\n");
    return ret;
}

int bq25792_configure_2s_charging()
{
    int fd = bq25792_open();
    if (fd < 0)
        return -1;

    // Set 3A input current limit → 3A ÷ 0.05A = 60
    if (bq25792_write_reg(fd, REG_INPUT_CURRENT_LIMIT, 60) < 0)
        goto fail;

    // Set 2A charge current → 2A ÷ 0.1A = 20
    if (bq25792_write_reg(fd, REG_CHARGE_CURRENT, 20) < 0)
        goto fail;

    // Set minimum VSYS to 7.0V → (7.0V - 3.0V) ÷ 0.1V = 40 = 0x0E (approx. from datasheet table)
    if (bq25792_write_reg(fd, REG_MIN_VSYS, 0x0E) < 0)
        goto fail;

    // VSYS regulation voltage: 8.6V → (8600mV - 3000mV) ÷ 10 = 560 = 0x0230
    if (bq25792_write_reg(fd, REG_VSYS_REG_1, 0x30) < 0)
        goto fail; // lower 8 bits
    if (bq25792_write_reg(fd, REG_VSYS_REG_2, 0x02) < 0)
        goto fail; // upper 2 bits

    printf("[BQ25792] 2S charging configured successfully\n");
    close(fd);
    return 0;

fail:
    perror("[BQ25792] Charging configuration failed");
    close(fd);
    return -1;
}
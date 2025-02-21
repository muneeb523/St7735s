#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#include <gpiod.h>
#include <linux/spi/spidev.h>

#include "st7735s_compat.h"

int spi_fd;
 struct gpiod_line_request *rst_request;
    struct gpiod_line_request *dc_request;
        unsigned int rst_line_offset =14;

#define SPI_DEVICE "/dev/spidev1.0"
#define SPI_DEFAULT_FREQ  15000000 // 8 MHz

typedef struct {
    int spi_fd;
    unsigned int rst_line_offset;
    unsigned int dc_line_offset;
    uint32_t _freq;
    int rotation;
    unsigned int _width;
    unsigned int _height;

} ST7735;

ST7735 st7735;

// Function to simulate the requestOutputLine (you should implement it according to your system)
 struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer)
    {
        struct gpiod_request_config *req_cfg = gpiod_request_config_new();
        struct gpiod_line_settings *settings = gpiod_line_settings_new();
        struct gpiod_line_config *line_cfg = gpiod_line_config_new();

        if (!req_cfg || !settings || !line_cfg)
           printf("Failed to allocate GPIO settings");

        gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
        gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
        gpiod_request_config_set_consumer(req_cfg, consumer);

        struct gpiod_chip *chip = gpiod_chip_open(chip_path);
        if (!chip)
            printf("Failed to open GPIO chip");

        struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

        gpiod_request_config_free(req_cfg);
        gpiod_line_config_free(line_cfg);
        gpiod_line_settings_free(settings);

        if (!request)
           printf("Failed to request GPIO line");

        return request;
    }

void ST7735_Init( const char* chip_path, unsigned int rst_offset, unsigned int dc_offset, uint32_t freq) {
    // Default values if freq is not provided
    if (freq == 0) {
        freq = SPI_DEFAULT_FREQ;
    }

    rst_request = requestOutputLine(chip_path, rst_offset, "RST");
    dc_request = requestOutputLine(chip_path, dc_offset, "DC");

    // Open the SPI device
    st7735.spi_fd = open(SPI_DEVICE, O_RDWR);
    if (st7735.spi_fd < 0) {
        perror("Failed to open SPI device");
        exit(1);
    }

    // Set the SPI mode to SPI_MODE_0
    uint8_t mode = SPI_MODE_0;
    if (ioctl(st7735.spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror(" SPI mode");
        close(st7735.spi_fd);
        exit(1);
    }

    // Set the SPI speed (frequency)
    if (ioctl(st7735.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &freq) < 0) {
        perror("Failed to set SPI frequency");
        close(st7735.spi_fd);
        exit(1);
    }

    // Initialize other parameters
    st7735.rst_line_offset = rst_offset;
    st7735.dc_line_offset = dc_offset;
   

}

    void setLineValue(unsigned int line_offset, enum gpiod_line_value value)
    {
        struct gpiod_line_request *request = (line_offset == rst_line_offset) ? rst_request : dc_request;

        // std::cout << "line_offset: " << line_offset
        //           << ", rst_line_offset: " << rst_line_offset
        //           << ", dc_offset: " << dc_line_offset << std::endl;

        if (!request || gpiod_line_request_set_value(request, line_offset, value) < 0)
        {
            printf("Failed to set GPIO line value");
        }
    }


  
void SPI_Init(void) {
    ST7735_Init("/dev/gpiochip5",14,13,8000000);

	// Pin_Low(CS);
}

void Pin_CS_Low(void) {
}

void Pin_CS_High(void) {
}

void Pin_RES_High(void) {

    setLineValue(14, GPIOD_LINE_VALUE_ACTIVE);

}

void Pin_RES_Low(void) {

  setLineValue(14, GPIOD_LINE_VALUE_INACTIVE);
}

void Pin_DC_High(void) {

    setLineValue(13, GPIOD_LINE_VALUE_ACTIVE);
}

void Pin_DC_Low(void) {

   setLineValue(13, GPIOD_LINE_VALUE_INACTIVE); // Command mode

}

extern uint8_t backlight_pct;
void Pin_BLK_Pct(uint8_t pct) {


}

void SPI_send(uint16_t len, uint8_t *data) {

	 struct spi_ioc_transfer tr = {};
    tr.speed_hz = SPI_DEFAULT_FREQ;
    tr.bits_per_word = 8;
    tr.tx_buf = (uintptr_t)data; // Pointing to data array
    tr.len = len; // Length to send


    // If the length is greater than 1 byte, send in chunks if needed
    while (len > 0) {
        // If the length to send is greater than what can be handled in one transfer, adjust
        uint16_t chunk_size = (len > 256) ? 256 : len; // Adjust the chunk size, here assuming 256 bytes is safe

        tr.len = chunk_size;

        // Perform the SPI transfer
        if (ioctl(st7735.spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            printf("failed spi");
            return ;
        }

        // Update data pointer and length for the next chunk
        data += chunk_size;
        len -= chunk_size;
    }

    // End transmission: Pull CS high
}

void SPI_TransmitCmd(uint16_t len, uint8_t *data) {
    Pin_DC_Low();
    SPI_send(len, data);
}

void SPI_TransmitData(uint16_t len, uint8_t *data) {
    Pin_DC_High();
    SPI_send(len, data);
}

void SPI_Transmit(uint16_t len, uint8_t *data) {

    SPI_TransmitCmd(1, data++);
    if (--len)
       SPI_TransmitData(len, data);

}

void _Delay(uint32_t d) {

    usleep(d*1000);

}

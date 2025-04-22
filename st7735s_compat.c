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
#include "appgpio.h"

int spi_fd;

#define GPIO_DEVICE4 "/dev/gpiochip3"
#define GPIO_LINE_LCD_RST 1
#define GPIO_LINE_LCD_DC 4
#define GPIO_LINE_LCD_LED 10
#define GPIO_DEVICE5 "/dev/gpiochip4"
#define GPIO_LINE_LCD_CS 13

#define SPI_DEVICE "/dev/spidev1.0"
#define SPI_DEFAULT_FREQ  15000000 // 8 MHz

typedef struct {
    int spi_fd;
    struct gpiod_line_request *rst_req;
    struct gpiod_line_request *cs_req;
    struct gpiod_line_request *dc_req;
    struct gpiod_line_request *led_req;
    uint32_t _freq;
    int rotation;
    unsigned int _width;
    unsigned int _height;
} ST7735;

ST7735 st7735;

void ST7735_Init( uint32_t freq ) {
    // Default values if freq is not provided
    if (freq == 0) {
        freq = SPI_DEFAULT_FREQ;
    }

    st7735.rst_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_LCD_RST, "LCD_RST");
    // st7735.cs_req  = requestOutputLine(GPIO_DEVICE5, GPIO_LINE_LCD_CS,  "LCD_CS");
    st7735.dc_req  = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_LCD_DC,  "LCD_DC");
    st7735.led_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_LCD_LED, "LCD_LED");

    // Open the SPI device
    st7735.spi_fd = open(SPI_DEVICE, O_RDWR);
    if (st7735.spi_fd < 0) {
        perror("Failed to open SPI device");
        exit(1);
    }

    // Set the SPI mode to SPI_MODE_0
    uint8_t mode = SPI_MODE_0;
    if (ioctl(st7735.spi_fd, SPI_IOC_WR_MODE, &mode) < 0) {
        perror("SPI mode");
        close(st7735.spi_fd);
        exit(1);
    }

    // Set the SPI speed (frequency)
    if (ioctl(st7735.spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &freq) < 0) {
        perror("Failed to set SPI frequency");
        close(st7735.spi_fd);
        exit(1);
    } 
}

void SPI_Init(void) 
{
    ST7735_Init( SPI_DEFAULT_FREQ );
	// Pin_CS_Low();
}

void Pin_CS_High(void) 
{
    // setLineValue(st7735.cs_req, GPIO_LINE_LCD_CS, GPIOD_LINE_VALUE_ACTIVE);
    // printf("Pin_CS_High\r\n");
}

void Pin_CS_Low(void) 
{
    // setLineValue(st7735.cs_req, GPIO_LINE_LCD_CS, GPIOD_LINE_VALUE_INACTIVE);
    // printf("Pin_CS_Low\r\n");
}

void Pin_RES_High(void) 
{
    setLineValue(st7735.rst_req, GPIO_LINE_LCD_RST, GPIOD_LINE_VALUE_ACTIVE);
    printf("Pin_RES_High\r\n");
}

void Pin_RES_Low(void) 
{
    setLineValue(st7735.rst_req, GPIO_LINE_LCD_RST, GPIOD_LINE_VALUE_INACTIVE);
    printf("Pin_RES_Low\r\n");
}

void Pin_DC_High(void) 
{
    setLineValue(st7735.dc_req, GPIO_LINE_LCD_DC, GPIOD_LINE_VALUE_ACTIVE);
}

void Pin_DC_Low(void) 
{
   setLineValue(st7735.dc_req, GPIO_LINE_LCD_DC, GPIOD_LINE_VALUE_INACTIVE); // Command mode
}

void Pin_BLK_Pct(uint8_t pct) 
{
    if( pct > 0 )
    {
        setLineValue(st7735.led_req, GPIO_LINE_LCD_LED, GPIOD_LINE_VALUE_ACTIVE);
    } 
    else 
    {
        setLineValue(st7735.led_req, GPIO_LINE_LCD_LED, GPIOD_LINE_VALUE_INACTIVE);
    }
}

void SPI_send(uint16_t len, uint8_t *data) 
{
    Pin_CS_Low();

	struct spi_ioc_transfer tr = {};
    tr.speed_hz = SPI_DEFAULT_FREQ;
    tr.bits_per_word = 8;
    tr.tx_buf = (uintptr_t)data; // Pointing to data array
    tr.len = len; // Length to send
    tr.cs_change = true;


    // If the length is greater than 1 byte, send in chunks if needed
    while (len > 0) {
        // If the length to send is greater than what can be handled in one transfer, adjust
        uint16_t chunk_size = (len > 256) ? 256 : len; // Adjust the chunk size, here assuming 256 bytes is safe

        tr.len = chunk_size;

        // Perform the SPI transfer
        if (ioctl(st7735.spi_fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
            printf("failed spi");
            return;
        }

        // Update data pointer and length for the next chunk
        data += chunk_size;
        len -= chunk_size;
    }

    // End transmission: Pull CS high
    Pin_CS_High();
}

void SPI_TransmitCmd(uint16_t len, uint8_t *data) 
{
    Pin_DC_Low();
    usleep(2);
    SPI_send(len, data);
}

void SPI_TransmitData(uint16_t len, uint8_t *data) 
{
    Pin_DC_High();
    usleep(2);
    SPI_send(len, data);
}

void SPI_Transmit(uint16_t len, uint8_t *data) 
{
    SPI_TransmitCmd(1, data++);
    if (--len) {
       SPI_TransmitData(len, data);
    }
}

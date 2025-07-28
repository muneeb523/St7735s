#include <iostream>
#include <chrono>
#include <algorithm>
#include <thread>
#include <cstring>
#include <ctime>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <cstdlib>
#include <sys/types.h>
#include <signal.h>
#include <sys/wait.h>
#include <cstdio>
#include <stdint.h>
#include <string.h>
#include <fstream>
#include <vector>
#include <cstdint>
#include <mutex>
#include <nlohmann/json.hpp> // JSON library: https://github.com/nlohmann/json
#include <atomic>
#include <time.h>
#include <curl/curl.h>
#include <condition_variable>
#include <optional>
#include <array>
#include <sys/stat.h> // for chmod()
#include <termios.h>
#include <fcntl.h>
#include <regex>

#define IMAGE_WIDTH 140
#define IMAGE_HEIGHT 60
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT)
#define GPIO_DEVICE4 "/dev/gpiochip3"
#define GPIO_DEVICE5 "/dev/gpiochip4"
#define GPIO_LINE_BA 4
#define GPIO_LINE_BB 5
#define GPIO_LINE_FLASH 7
#define GPIO_CHARGE_DISABLE 15
#define GPIO_LINE_GPS_PWR_EN 29

std::atomic<bool> buzzer_running = false;

typedef struct
{
    struct gpiod_line_request *ba_req;
    struct gpiod_line_request *bb_req;
    struct gpiod_line_request *flash_req;
    struct gpiod_line_request *charge_disable;
    struct gpiod_line_request *gps_pwr_en;

} TestGpioReq;

TestGpioReq testGpioReq;
extern "C"
{
#include "../gps_gnss.h"
#include "../appgpio.h"
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
#include "../st7735s_compat.h"
#include "../screen.h"
}

// Function declarations
void lcdTest();
void buzzerOn();
void buzzerOff();
void flashlightOn();
void flashlightOff();
void gpsGnssTest();
void updateBuzzer();

void initPeripherals()
{
    testGpioReq.ba_req = requestOutputLine(GPIO_DEVICE5, GPIO_LINE_BA, "BUZZER_A");
    testGpioReq.bb_req = requestOutputLine(GPIO_DEVICE5, GPIO_LINE_BB, "BUZZER_B");
    testGpioReq.flash_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_FLASH, "FLASHLIGHT");
    testGpioReq.gps_pwr_en = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_GPS_PWR_EN, "GPS POWER ENABLE");
    testGpioReq.charge_disable = requestOutputLine(GPIO_DEVICE4, GPIO_CHARGE_DISABLE, "CHARGE DISABLE");
}

int main(int argc, char *argv[])
{

    if (argc < 2)
    {
        std::cerr << "Usage: application.exe <command> [option]\n";
        return 1;
    }

    initButtons();
    initPeripherals();
    buzzer_thread = std::thread(updateBuzzer, this);
    buzzer_thread.detach(); // Detach the buzzer thread

    std::string command = argv[1];

    if (command == "lcd")
    {
        lcdTest();
    }
    else if (command == "buzzer")
    {
        if (argc < 3)
        {
            std::cerr << "Usage: application.exe buzzer <on|off>\n";
            return 1;
        }
        std::string action = argv[2];
        if (action == "on")
        {
            buzzerOn();
        }
        else if (action == "off")
        {
            buzzerOff();
        }
        else
        {
            std::cerr << "Invalid buzzer command. Use on or off.\n";
            return 1;
        }
    }
    else if (command == "flashlight")
    {
        if (argc < 3)
        {
            std::cerr << "Usage: application.exe flashlight <on|off>\n";
            return 1;
        }
        std::string action = argv[2];
        if (action == "on")
        {
            flashlightOn();
        }
        else if (action == "off")
        {
            flashlightOff();
        }
        else
        {
            std::cerr << "Invalid flashlight command. Use on or off.\n";
            return 1;
        }
    }
    else if (command == "gps_gnss")
    {
        gpsGnssTest();
    }
    else
    {
        std::cerr << "Unknown command: " << command << "\n";
        return 1;
    }

    return 0;
}

// Function definitions

void lcdTest()
{
    std::cout << "[LCD Test] Function called.\n";
    ST7735S_Init();
    setOrientation(R90);
    fillScreen();
    flushBuffer();
    setColor(0, 0, 0); // Black background
    drawImage(5, 7, battery_level2, 16, 16);
}

void buzzerOn()
{
    std::cout << "[Buzzer] Turn ON called.\n";
    buzzer_running.store(true);
}

void buzzerOff()
{
    std::cout << "[Buzzer] Turn OFF called.\n";
    buzzer_running.store(false);
}

void flashlightOn()
{
    std::cout << "[Flashlight] Turn ON called.\n";
    setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_ACTIVE);
    // Add logic to turn on the flashlight here
}

void flashlightOff()
{
    std::cout << "[Flashlight] Turn OFF called.\n";
    setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_INACTIVE);
    // Add logic to turn off the flashlight here
}

void gpsGnssTest()
{
    std::cout << "[GPS/GNSS Test] Function called.\n";
    setLineValue(testGpioReq.gps_pwr_en, GPIO_LINE_GPS_PWR_EN, GPIOD_LINE_VALUE_ACTIVE);
    usleep(4000000); //<4 sec delay
    int fd = gps_i2c_init("/dev/i2c-2");
    if (fd < 0)
    {
        fprintf(stderr, "  ' Failed to initialize GPS Device not present ' / ' detected after turning on the power ' \n");
        sleep(30);
        continue;
    }

    double lat = 0.0, lon = 0.0;
    if (gps_get_location(fd, &lat, &lon) == 0)
    {
        printf("Latitude: %.6f, Longitude: %.6f\n", lat, lon);
    }
    else
    {
        fprintf(stderr, "Failed to get valid GPS location\n");
    }

    gps_i2c_close(fd);
    usleep(100000);
}

void updateBuzzer()
{
    while (true)
    {
        if (buzzer_running.load())
        {

            setLineValue(testGpioReq.ba_req, GPIO_LINE_BA, GPIOD_LINE_VALUE_ACTIVE);
            setLineValue(testGpioReq.bb_req, GPIO_LINE_BB, GPIOD_LINE_VALUE_INACTIVE);
            std::this_thread::sleep_for(std::chrono::microseconds(125));
            setLineValue(testGpioReq.ba_req, GPIO_LINE_BA, GPIOD_LINE_VALUE_ACTIVE);
            setLineValue(testGpioReq.bb_req, GPIO_LINE_BB, GPIOD_LINE_VALUE_ACTIVE);
            std::this_thread::sleep_for(std::chrono::microseconds(125));
            setLineValue(testGpioReq.ba_req, GPIO_LINE_BA, GPIOD_LINE_VALUE_INACTIVE);
            setLineValue(testGpioReq.bb_req, GPIO_LINE_BB, GPIOD_LINE_VALUE_ACTIVE);
            std::this_thread::sleep_for(std::chrono::microseconds(125));
            setLineValue(testGpioReq.ba_req, GPIO_LINE_BA, GPIOD_LINE_VALUE_INACTIVE);
            setLineValue(testGpioReq.bb_req, GPIO_LINE_BB, GPIOD_LINE_VALUE_INACTIVE);
            std::this_thread::sleep_for(std::chrono::microseconds(125));
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::seconds(1)); // Update every minute
        }
    }
}
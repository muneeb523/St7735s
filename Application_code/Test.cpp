#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <ctime>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
#include <atomic>

extern "C" {

#include <gpiod.h>
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
#include "../st7735s_compat.h"
#include "../screen.h"
}

#define NTP_TIMESTAMP_DELTA 2208988800ULL

enum Mode {
    STREAM,
    BUZZER,
    VoIP,
    FLASHLIGHT
};

struct ImageSize {
    const uint16_t *image;
    int width;
    int height;
};

// Mode images
ImageSize modeImages[7] = {
    {None, 80, 60},  // Default (None)
    {Mode1, 80, 60}, // Mode1 VoIP, STREAM
    {Mode2, 80, 60}, // Mode2 BUZZER, FLASHLIGHT
    {Mode3, 80, 60}, // Mode3 FLASHLIGHT, VoIP
    {Mode4, 80, 60}, // Mode4 STREAM, FLASHLIGHT
    {Mode5, 80, 60}, // Mode5 STREAM, BUZZER
    {Mode6, 80, 60}  // Mode6 VoIP, BUZZER
};

std::atomic<int> currentMode(0);
std::atomic<bool> modeConfirmed(false);
std::atomic<bool> buttonPressed(false);
std::chrono::time_point<std::chrono::steady_clock> lastPressTime;

// GPIO Configuration
const char *const CHIP_PATH = "/dev/gpiochip3";
const unsigned int LINE_OFFSET = 2;
struct gpiod_line_request *button_request;
std::string currentTime = "00:00"; // Default Time


class DisplayExample {
public:
    void run() {

        Button_Init();
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();

        // Start threads
        std::thread buttonThread(&DisplayExample::buttonPressDetection, this);
        std::thread ntpThread(&DisplayExample::updateNTPTime, this);
        buttonThread.detach();
        ntpThread.detach();

        drawUI();

        while (true) {

            if (modeConfirmed.load()) {
                drawSelectedMode();
                modeConfirmed.store(false);
            } 
            else {
                drawUI();
            }
        }
    }

    void drawSelectedMode() {
        setColor(0, 0, 0);
        fillScreen();
        setFont(ter_u12b);
        drawText(20, 40, "Mode Selected");
        flushBuffer();
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    void drawUI() {
        setColor(0, 0, 0);
        fillScreen();

        // Battery and Signal icons
        drawImage(5, 7, battery_level2, 16, 16);
        drawImage(55, 7, signal_level1, 19, 16);

        // Draw selected mode image
        int index = currentMode.load();
        drawImage(0, 60, modeImages[index].image, modeImages[index].width, modeImages[index].height);

        // Display NTP Time
        setColor(31, 63, 31);
        setbgColor(0, 0, 0);
        setFont(ter_u12b);
        drawText(25, 35, currentTime.c_str());

        flushBuffer();
    }

    void buttonPressDetection()
    {
        while (true)
        {
            enum gpiod_line_value value = gpiod_line_request_get_value(button_request, LINE_OFFSET);
            auto now = std::chrono::steady_clock::now();
    
            if (value == GPIOD_LINE_VALUE_INACTIVE)
            {
                if (!buttonPressed.load())  // Only register new presses
                {
                    buttonPressed.store(true);
                    lastPressTime = now;
                    currentMode.store((currentMode.load() + 1) % 7); // Cycle through modes
                }
                else if (std::chrono::duration_cast<std::chrono::seconds>(now - lastPressTime).count() >= 3)
                {
                    modeConfirmed.store(true);
                    buttonPressed.store(false); // Reset detection for next cycle
                }
            }
            else
            {
                buttonPressed.store(false); // Reset if button is released
            }
    
            std::this_thread::sleep_for(std::chrono::milliseconds(50)); // Polling interval
        }
    }
    

    void updateNTPTime() {
        while (true) {
            std::string timeStr = getNTPTime();
            if (!timeStr.empty()) {
                currentTime = timeStr; // Update time from NTP server
            } else {
                currentTime = "00:00"; // Default time if NTP request fails
            }
            std::this_thread::sleep_for(std::chrono::minutes(1)); // Update every minute
        }
    }
    

    std::string getNTPTime() {
        const char *ntpServer = "pool.ntp.org";
        int port = 123;
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0) {
            perror("Socket creation failed");
            return "";
        }

        struct sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, "129.6.15.28", &serverAddr.sin_addr);

        uint8_t packet[48] = {0};
        packet[0] = 0b11100011; // LI, Version, Mode

        if (sendto(sockfd, packet, sizeof(packet), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
            perror("Failed to send NTP request");
            close(sockfd);
            return "";
        }

        struct sockaddr_in responseAddr;
        socklen_t addrLen = sizeof(responseAddr);
        if (recvfrom(sockfd, packet, sizeof(packet), 0, (struct sockaddr *)&responseAddr, &addrLen) < 0) {
            perror("Failed to receive NTP response");
            close(sockfd);
            return "";
        }

        close(sockfd);

        uint32_t timestamp;
        memcpy(&timestamp, &packet[40], sizeof(timestamp));
        timestamp = ntohl(timestamp);

        time_t unixTime = timestamp - NTP_TIMESTAMP_DELTA;
        struct tm *timeInfo = localtime(&unixTime);

        std::ostringstream timeStream;
        timeStream << (timeInfo->tm_hour < 10 ? "0" : "") << timeInfo->tm_hour << ":"
                   << (timeInfo->tm_min < 10 ? "0" : "") << timeInfo->tm_min;

        return timeStream.str();
    }
};

int main() {
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

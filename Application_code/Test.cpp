#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <ctime>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>

extern "C"
{
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
#include "../st7735s_compat.h"
#include "../screen.h"
}

#define NTP_TIMESTAMP_DELTA 2208988800ULL

enum Mode
{
    STREAM,
    BUZZER,
    VoIP,
    FLASHLIGHT
};

struct ImageSize
{
    const uint16_t *image;
    int width;
    int height;
};

int i = 1;
Mode current_mode = STREAM;
std::string currentTime = "00:00"; // Default Time

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();

        // Start NTP time update in a separate thread
        std::thread ntpThread(&DisplayExample::updateNTPTime, this);
        ntpThread.detach();

        while (true)
        {
            drawUI();
            waitForButtonPress();
        }
    }

    void drawUI()
    {
        setColor(0, 0, 0); // Black background
  

        // Battery and Signal icons
        drawImage(5, 7, battery_level2, 16, 16);
        drawImage(55, 7, signal_level1, 19, 16);

        // Modes with their respective image names & sizes
        ImageSize modeImages[6] = {
            {Mode1, 80, 60},
            {Mode2, 80, 60},
            {Mode3, 80, 60},
            {Mode4, 80, 60},
            {Mode5, 80, 60},
            {Mode6, 80, 60}};

        // Draw mode image at fixed position
        drawImage(0, 60, modeImages[i].image, modeImages[i].width, modeImages[i].height);

        
        // Display NTP Time in "HH:MM" format
        setColor(31, 63, 31); // Green text
        setbgColor(0, 0, 0);  // Black background
        setFont(ter_u12b);    // Smallest readable font
        //printf("Displayed Time on Screen: %s\n", currentTime.c_str());
        drawText(25, 35, currentTime.c_str());

        flushBuffer();
    }

    void waitForButtonPress()
    {
        while (!isButtonPressed())
        {
        }
        updateMode();
        _Delay(5000);
    }

    bool isButtonPressed()
    {
        static int counter = 0;
        counter++;
        return (counter % 500000 == 0);
        _Delay(10000);
    }

    void updateMode()
    {
        i = (i + 1) % 6; // Cycle through 6 modes
    }

    void updateNTPTime()
    {
        while (true)
        {
            std::string timeStr = getNTPTime();
            if (!timeStr.empty())
            {
                currentTime = timeStr;
            }
            std::this_thread::sleep_for(std::chrono::minutes(1)); // Update every minute
        }
    }

    std::string getNTPTime()
    {
        const char *ntpServer = "pool.ntp.org";
        int port = 123;
        int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd < 0)
        {
            perror("Socket creation failed");
            return "";
        }

        struct sockaddr_in serverAddr{};
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);
        inet_pton(AF_INET, "129.6.15.28", &serverAddr.sin_addr); // NIST NTP Server

        uint8_t packet[48] = {0};
        packet[0] = 0b11100011; // LI, Version, Mode

        if (sendto(sockfd, packet, sizeof(packet), 0, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0)
        {
            perror("Failed to send NTP request");
            close(sockfd);
            return "";
        }

        struct sockaddr_in responseAddr;
        socklen_t addrLen = sizeof(responseAddr);
        if (recvfrom(sockfd, packet, sizeof(packet), 0, (struct sockaddr *)&responseAddr, &addrLen) < 0)
        {
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

int main()
{
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

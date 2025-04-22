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
#include "../appgpio.h"
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
#include "../st7735s_compat.h"
#include "../screen.h"
}

#define NTP_TIMESTAMP_DELTA 2208988800ULL
#define TZ_DELTA 7*60*60

#define GPIO_DEVICE4 "/dev/gpiochip3"
#define GPIO_LINE_BA 18
#define GPIO_LINE_BB 3
#define GPIO_LINE_FLASH 7

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

typedef struct {
    struct gpiod_line_request *ba_req;
    struct gpiod_line_request *bb_req;
    struct gpiod_line_request *flash_req;
} TestGpioReq;

TestGpioReq testGpioReq;

int mode, nmode = 1;
time_t mode_change_time = 1;
Mode current_mode = STREAM;
std::string currentTime = "00:00"; // Default Time

int videoRunning = 0;
time_t videoStart = 0;
char videoTime[6] = "00:00"; // Default Time

int buzzerRunning = 0;

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();
        std::cout << "flushed" << std::endl;

        initButtons();
        initPeripherals();

        // Start NTP time update in a separate thread
        std::thread ntpThread(&DisplayExample::updateNTPTime, this);
        ntpThread.detach();

        std::cout << "NTP" << std::endl;

        while (true)
        {
            drawUI();
            processMode();
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
        ImageSize modeImages[7] = {
            {None,  80, 60},
            {Mode1, 80, 60},
            {Mode2, 80, 60},
            {Mode3, 80, 60},
            {Mode4, 80, 60},
            {Mode5, 80, 60},
            {Mode6, 80, 60}};

        // Draw mode image at fixed position
        drawImage(0, 60, modeImages[mode].image, modeImages[mode].width, modeImages[mode].height);

        
        // Display NTP Time in "HH:MM" format
        setColor(31, 63, 31); // Green text
        setbgColor(0, 0, 0);  // Black background
        setFont(ter_u12b);    // Smallest readable font
        printf("Displayed Time on Screen: %s\n", currentTime.c_str());
        drawText(25, 35, currentTime.c_str());

        if(videoRunning) {
            printf("videoRunning: %s\n", videoTime);
            setColor(31, 63, 31); // Green text
            setbgColor(0, 0, 0);  // Black background
            setFont(ter_u12b);    // Smallest readable font
            drawText(25, 125, videoTime);
        }

        flushBuffer();
    }

    /**
    * Wait for either button to be pressed and update mode
    */
    void waitForButtonPress(void) {
        int pressed = 0;
        while (pressed == 0) {
            pressed = areButtonsPressed();
            usleep(10000);  // 10ms sleep to avoid CPU hogging
        }
        printf("areButtonsPressed %d\r\n", pressed);
        if(pressed > 0) {
            updateMode(pressed);
        }
        _Delay(5000);  // Assuming microseconds (5ms)
    }

    void updateMode(int btn)
    {
        mode = (mode + 1) % 7; // Cycle through 6 modes
        if(btn & 0x02) {
            // okay to go to alarm modes
        } else {
            if(mode == 2) { mode = 3; }
            if(mode >= 5) { mode = 0; }
        }
        mode_change_time = time(NULL);
    }

    void processMode()
    {
        if(mode_change_time != 0)
        {
            time_t now = time(NULL);
            if(now - mode_change_time > 3)
            {
                mode_change_time = 0;
                if(mode == 0) {
                    alarmOff();
                    lightOff();
                    videoOff();
                    voipOff();
                } else if(mode == 1) {
                    alarmOff();
                    lightOff();
                    videoOn();
                    voipOn();
                } else if(mode == 2) {
                    alarmOn();
                    lightOn();
                    videoOff();
                    voipOff();
                } else if(mode == 3) {
                    alarmOff();
                    lightOn();
                    videoOff();
                    voipOn();
                } else if(mode == 4) {
                    alarmOff();
                    lightOn();
                    videoOn();
                    voipOff();
                } else if(mode == 5) {
                    alarmOn();
                    lightOff();
                    videoOn();
                    voipOff();
                } else if(mode == 6) {
                    alarmOn();
                    lightOff();
                    videoOff();
                    voipOn();
                }
            }
        }
        if(videoRunning)
        {
            time_t now = time(NULL);
            time_t videoSec = now - videoStart;
            int minutes = (videoSec / 60) % 100;
            int seconds = videoSec % 60;
            sprintf(videoTime, "%02d:%02d", minutes, seconds);
        }
    }

    void initPeripherals() {
        testGpioReq.ba_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_BA, "BUZZER_A");
        testGpioReq.bb_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_BB,  "BUZZER_B");
        testGpioReq.flash_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_FLASH, "FLASHLIGHT");
    }

    void alarmOff() {
        printf("alarmOff\r\n");
        buzzerRunning = 0;
    }

    void lightOff() {
        printf("lightOff\r\n");
        setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_INACTIVE);
    }

    void videoOff() {
        printf("videoOff\r\n");
        videoRunning = 0;
        videoStart = 0;
        sprintf(videoTime, "%02d:%02d", 0, 0);
    }
    void voipOff() {
        printf("voipOff\r\n");
    }

    void alarmOn() {
        printf("alarmOn\r\n");
        if(!buzzerRunning)
        {
            buzzerRunning = 1;
            // std::thread buzzerThread(&DisplayExample::updateBuzzer, this);
            // buzzerThread.detach();
        }
    }

    void lightOn() {
        printf("lightOn\r\n");
        setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_ACTIVE);
    }

    void videoOn() {
        printf("videoOn\r\n");
        if(!videoRunning)
        {
            videoRunning = 1;
            videoStart = time(NULL);
        }
    }
    void voipOn() {
        printf("voipOn\r\n");
    }

    void updateBuzzer()
    {
        while(buzzerRunning)
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

        time_t unixTime = timestamp - NTP_TIMESTAMP_DELTA - TZ_DELTA;
        struct tm *timeInfo = localtime(&unixTime);

        std::ostringstream timeStream;
        timeStream << (timeInfo->tm_hour < 10 ? "0" : "") << timeInfo->tm_hour << ":"
                   << (timeInfo->tm_min < 10 ? "0" : "") << timeInfo->tm_min;

        return timeStream.str();
    }
};

int main()
{
    forceGPIOs();
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}
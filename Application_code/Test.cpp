#include <iostream>
#include <chrono>
#include <thread>
#include <cstring>
#include <ctime>
#include <arpa/inet.h>
#include <unistd.h>
#include <sstream>
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

#define IMAGE_WIDTH 140
#define IMAGE_HEIGHT 60
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT)
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
#define TZ_DELTA 7 * 60 * 60

#define GPIO_DEVICE4 "/dev/gpiochip3"
#define GPIO_LINE_BA 18
#define GPIO_LINE_BB 3
#define GPIO_LINE_FLASH 7
#define GPIO_LINE_SELF_KILL 20

using json = nlohmann::json;
std::mutex state_mutex;
std::atomic<bool> state_dirty = false;
std::atomic<bool> running = true;

std::thread buzzer_thread;
std::atomic<bool> buzzer_running = false;
std::mutex buzzer_mutex;

struct DeviceState
{

    std::string light_mode = "static";
    int light_brightness = 128;
    volatile bool alarm_on = false;
    std::string alarm_sound = "siren";
    volatile bool camera_recording = false;
    int battery_level = 50;
    volatile bool battery_charging = false;
    float gps_latitude = 0.0;
    float gps_longitude = 0.0;
    bool wifi_connected = false;
    std::string wifi_ssid = "";
    int wifi_strength = 0;
    volatile bool in_emergency = false;
    bool cellular_connected = false;
    int cellular_strength = 0;
};

DeviceState current_state;

// //<Usage
// #### Stream Video from a Live Source (Emergency Mode )

//     /usr/bin/Flashlight  <stream_name> <compression_type>

// #### Stream Video to Local Storage

//      /usr/bin/Flashlight <stream_name> <compression_type> local_storage

// #### Stream Video from Files

//    /usr/bin/Flashlight <stream_name> <compression_type> <file1_path> <file2_path> <file3_path>

uint16_t barcode[IMAGE_SIZE];
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

typedef struct
{
    struct gpiod_line_request *ba_req;
    struct gpiod_line_request *bb_req;
    struct gpiod_line_request *flash_req;
    struct gpiod_line_request *self_kill;
} TestGpioReq;

TestGpioReq testGpioReq;

pid_t gst_pid = -1;
volatile bool barcode_show = false;
int mode, nmode = 1;
time_t mode_change_time = 1;
Mode current_mode = STREAM;
std::string currentTime = "00:00"; // Default Time

int videoRunning = 0;
time_t videoStart = 0;
char videoTime[6] = "00:00"; // Default Time

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();
        loadBarcodeImage("/home/barcode/image.raw", barcode, IMAGE_SIZE);
        std::cout << "flushed" << std::endl;

        initButtons();
        initPeripherals();

        // Start NTP time update in a separate thread
        std::thread ntpThread(&DisplayExample::updateNTPTime, this);
        ntpThread.detach();

        std::thread shadowThread(&DisplayExample::shadowUpdateThread, this);
        shadowThread.detach();

        buzzer_thread = std::thread(&DisplayExample::updateBuzzer, this);
        buzzer_thread.detach(); // Detach the buzzer thread

        std::cout << "NTP" << std::endl;

        while (true)
        {
            drawUI();
            processMode();
            waitForButtonPress();
        }
    }
    bool loadBarcodeImage(const char *path, uint16_t *buffer, size_t size)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
        {
            std::cerr << "Failed to open barcode image file: " << path << std::endl;
            return false;
        }

        file.read(reinterpret_cast<char *>(buffer), size * sizeof(uint16_t));
        if (file.gcount() != static_cast<std::streamsize>(size * sizeof(uint16_t)))
        {
            std::cerr << "Error: Failed to read full barcode image." << std::endl;
            return false;
        }

        return true;
    }

    void drawUI()
    {
        setColor(0, 0, 0); // Black background

        if (barcode_show)
        {

            setColor(255, 255, 255); // white background

            drawImage(10, 10, barcode, IMAGE_WIDTH, IMAGE_HEIGHT);
        }
        else
        {

            // Battery and Signal icons
            drawImage(5, 7, battery_level2, 16, 16);
            drawImage(55, 7, signal_level1, 19, 16);

            // Modes with their respective image names & sizes
            ImageSize modeImages[7] = {
                {None, 80, 60},
                {Mode1, 80, 60},
                {Mode2, 80, 60},
                {Mode3, 80, 60},
                {Mode4, 80, 60},
                {Mode5, 80, 60},
                {Mode6, 80, 60}

            };

            // Draw mode image at fixed position
            drawImage(0, 60, modeImages[mode].image, modeImages[mode].width, modeImages[mode].height);

            // Display NTP Time in "HH:MM" format
            setColor(31, 63, 31); // Green text
            setbgColor(0, 0, 0);  // Black background
            setFont(ter_u12b);    // Smallest readable font
            printf("Displayed Time on Screen: %s\n", currentTime.c_str());
            drawText(25, 35, currentTime.c_str());

            if (videoRunning)
            {
                printf("videoRunning: %s\n", videoTime);
                setColor(31, 63, 31); // Green text
                setbgColor(0, 0, 0);  // Black background
                setFont(ter_u12b);    // Smallest readable font
                drawText(25, 125, videoTime);
            }
        }

        flushBuffer();
    }

    void shadowUpdateThread()
    {
        while (true)
        {
            if (state_dirty.load())
            {
                update_shadow_json();
                state_dirty = false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(60)); // configurable delay
        }
    }

    void mark_state_dirty()
    {
        state_dirty = true;
    }

    void update_shadow_json()
    {
        std::lock_guard<std::mutex> lock(state_mutex);

        json shadow;
        shadow["state"]["reported"] = {

            {"light_mode", current_state.light_mode},
            {"light_brightness", current_state.light_brightness},
            {"alarm_on", current_state.alarm_on},
            {"alarm_sound", current_state.alarm_sound},
            {"camera_recording", current_state.camera_recording},
            {"battery_level", current_state.battery_level},
            {"battery_charging", current_state.battery_charging},
            {"gps_latitude", current_state.gps_latitude},
            {"gps_longitude", current_state.gps_longitude},
            {"wifi_connected", current_state.wifi_connected},
            {"wifi_ssid", current_state.wifi_ssid},
            {"wifi_strength", current_state.wifi_strength},
            {"in_emergency", current_state.in_emergency},
            {"cellular_connected", current_state.cellular_connected},
            {"cellular_strength", current_state.cellular_strength}};

        std::string tmp_path = "/etc/aws_iot_device/shadow-input.json.tmp";
        std::string final_path = "/etc/aws_iot_device/shadow-input.json";

        std::ofstream tmp_file(tmp_path);
        if (tmp_file.is_open())
        {
            tmp_file << shadow.dump(4);
            tmp_file.close();

            if (std::rename(tmp_path.c_str(), final_path.c_str()) != 0)
            {
                std::cerr << "[Shadow] Failed to rename temp shadow file to final\n";
            }
        }
        else
        {
            std::cerr << "[Shadow] Failed to open temp shadow file\n";
        }
    }

    /**
     * Wait for either button to be pressed and update mode
     */
    void waitForButtonPress(void)
    {
        int pressed = 0;
        while (pressed == 0)
        {
            pressed = areButtonsPressed();
            usleep(10000); // 10ms sleep to avoid CPU hogging
        }
        printf("areButtonsPressed %d\r\n", pressed);
        if (pressed > 0)
        {
            updateMode(pressed);
        }
        _Delay(5000); // Assuming microseconds (5ms)
    }

    void updateMode(int btn)
    {
        std::lock_guard<std::mutex> lock(state_mutex); // Lock shared state access
        // Update mode cyclically through 0 to 6
        mode = (mode + 1) % 7;

        if (btn == 2)
        {
            // If button 1 (bit 1) is pressed, enter emergency mode
            if (!current_state.in_emergency)
            {
                current_state.in_emergency = true;
                // Possibly trigger an alert or a different mode?
            }
        }
        else if (btn == 1)
        {
            // If button 1 is not pressed and we are in emergency mode, reset it
            if (current_state.in_emergency)
            {
                mode = 0;                           // Reset mode
                current_state.in_emergency = false; // Exit emergency mode
            }

            if (barcode_show)
            {
                setOrientation(R90);

                barcode_show = false;
            }

            // Logical filtering of allowed modes
            if (mode == 2)
            {
                mode = 3; // Skip mode 2 if not in emergency
            }

            if (mode >= 5)
            {
                mode = 0; // Reset mode if above 4
            }
        }
        else if (btn == 3)
        {
            setOrientation(R180);
            barcode_show = true;
        }

        mark_state_dirty();

        mode_change_time = time(NULL); // Record the time of mode change
    }

    void processMode()
    {
        std::lock_guard<std::mutex> lock(state_mutex); // Lock shared state access

        if (mode_change_time != 0 && !current_state.in_emergency && !barcode_show)
        {
            time_t now = time(NULL);
            if (now - mode_change_time > 3)
            {
                mode_change_time = 0;
                if (mode == 0)
                {
                    alarmOff();
                    lightOff();
                    videoOff();
                    voipOff();
                }
                else if (mode == 1)
                {
                    alarmOff();
                    lightOff();
                    videoOn();
                    voipOn();
                }
                else if (mode == 2)
                {
                    alarmOn();
                    lightOn();
                    videoOff();
                    voipOff();
                }
                else if (mode == 3)
                {
                    alarmOff();
                    lightOn();
                    videoOff();
                    voipOn();
                }
                else if (mode == 4)
                {
                    alarmOff();
                    lightOn();
                    videoOn();
                    voipOff();
                }
                else if (mode == 5)
                {
                    alarmOn();
                    lightOff();
                    videoOn();
                    voipOff();
                }
                else if (mode == 6)
                {
                    alarmOn();
                    lightOff();
                    videoOff();
                    voipOn();
                }
            }
            mark_state_dirty();
        }
        else if (current_state.in_emergency)
        {
            lightOn();
            alarmOn();
            emergency_stream_on();
            voipOn();

            mark_state_dirty();
        }
        if (videoRunning)
        {
            time_t now = time(NULL);
            time_t videoSec = now - videoStart;
            int minutes = (videoSec / 60) % 100;
            int seconds = videoSec % 60;
            sprintf(videoTime, "%02d:%02d", minutes, seconds);
        }
    }

    void initPeripherals()
    {

        testGpioReq.ba_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_BA, "BUZZER_A");
        testGpioReq.bb_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_BB, "BUZZER_B");
        testGpioReq.flash_req = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_FLASH, "FLASHLIGHT");
        testGpioReq.self_kill = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_SELF_KILL, "SELF_KILL");
    }

    void emergency_stream_on()
    {

        printf("videoOn\r\n");
        if (!videoRunning)
        {
            if (gst_pid == -1)
            {
                gst_pid = fork();
                if (gst_pid == 0)
                {
                    videoRunning = 1;
                    videoStart = time(NULL);
                    // Child process: replace this process with the streaming app
                    execl("/usr/bin/Flashlight", "Flashlight", "demo-stream", "h265", nullptr);
                    perror("execl failed");
                    _exit(1); // In case execl fails
                }
                else
                {
                    printf("Started GStreamer process with PID: %d\n", gst_pid);
                }
            }
            else
            {
                printf("Stream already running (PID: %d)\n", gst_pid);
            }
        }
    }

    void alarmOff()
    {
        std::lock_guard<std::mutex> lock(buzzer_mutex);
        printf("alarmOff\r\n");
        current_state.alarm_on = false;
        buzzer_running = false;
    }

    void lightOff()
    {
        printf("lightOff\r\n");
        setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_INACTIVE);
    }

    void videoOff()
    {
        current_state.camera_recording = false;

        printf("videoOff\r\n");
        if (gst_pid != -1)
        {

            printf(" Stopping Streaming  (PID: %d)\n", gst_pid);
            kill(gst_pid, SIGINT);        // Send SIGINT (same as Ctrl+C)
            waitpid(gst_pid, nullptr, 0); // Wait for it to terminate
            gst_pid = -1;
            printf(" Flashlight stopped successfully\n");
        }
        else
        {
            printf(" No stream running to stop\n");
        }
        videoRunning = 0;
        videoStart = 0;
        sprintf(videoTime, "%02d:%02d", 0, 0);
    }
    void voipOff()
    {
        printf("voipOff\r\n");
    }

    void alarmOn()
    {
        std::lock_guard<std::mutex> lock(buzzer_mutex);

        printf("alarmOn\r\n");
        if (!buzzer_running)
        {
            current_state.alarm_on = true;
            buzzer_running = true;
        }
    }

    void lightOn()
    {
        printf("lightOn\r\n");
        setLineValue(testGpioReq.flash_req, GPIO_LINE_FLASH, GPIOD_LINE_VALUE_ACTIVE);
    }

    void videoOn()
    {
        current_state.camera_recording = true;

        printf("videoOn\r\n");
        if (!videoRunning)
        {

            if (gst_pid == -1)
            {
                gst_pid = fork();
                if (gst_pid == 0)
                {
                    videoRunning = 1;
                    videoStart = time(NULL);
                    // Child process: replace this process with the streaming app
                    execl("/usr/bin/Flashlight", "Flashlight", "demo-stream", "h265", "local_storage", nullptr);
                    perror("execl failed");
                    _exit(1); // In case execl fails
                }
                else
                {
                    printf("Started GStreamer process with PID: %d\n", gst_pid);
                }
            }
            else
            {
                printf("Stream already running (PID: %d)\n", gst_pid);
            }
        }
    }
    void voipOn()
    {
        printf("voipOn\r\n");
    }

    void updateBuzzer()
    {
        while (true)
        {
            bool local_buzzer_running = false;

            // Safely read shared flag
            {
                std::lock_guard<std::mutex> lock(buzzer_mutex);
                local_buzzer_running = buzzer_running;
            }

            if (local_buzzer_running)
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
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
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
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}
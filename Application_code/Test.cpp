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
#define IMAGE_WIDTH 140
#define IMAGE_HEIGHT 60
#define IMAGE_SIZE (IMAGE_WIDTH * IMAGE_HEIGHT)

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
typedef enum
{
    EMERGENCY,
    IDLE,
    BARCODE,
    RECORD
} SystemState;
SystemState currentState = IDLE;
#define NTP_TIMESTAMP_DELTA 2208988800ULL
#define TZ_DELTA 7 * 60 * 60

#define GPIO_DEVICE4 "/dev/gpiochip3"
#define GPIO_LINE_BA 18
#define GPIO_LINE_BB 3
#define GPIO_LINE_FLASH 7
#define GPIO_LINE_SELF_KILL 20
#define GPIO_LINE_GPS_PWR_EN 29

using json = nlohmann::json;
extern char **environ;
std::mutex state_mutex;
std::atomic<bool> state_dirty = false;
std::atomic<bool> running = true;

std::thread buzzer_thread;
std::atomic<bool> buzzer_running = false;
std::atomic<int> buzzer_frequency_hz;
std::mutex buzzer_mutex;
namespace fs = std::filesystem;
std::string getActiveNetworkType(); // Assume already implemented
volatile bool local_kvs_streaming = false;
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
    struct gpiod_line_request *gps_pwr_en;

} TestGpioReq;

TestGpioReq testGpioReq;

pid_t gst_pid = -1;
volatile bool barcode_show = false;
int mode, nmode = 1;
volatile int Disp_mode = 0;
time_t mode_change_time = 1;
Mode current_mode = STREAM;
std::string currentTime = "00:00"; // Default Time

int videoRunning = 0;
time_t videoStart = 0;
char videoTime[6] = "00:00"; // Default Time
std::atomic<bool> activityDetected{false};
std::thread inactivityThread;
std::thread Stream_Wifi;

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

        inactivityThread = std::thread(&DisplayExample::trackInactivity, this);
        inactivityThread.detach();

        Stream_Wifi = std::thread(&DisplayExample::Local_KVS, this);
        Stream_Wifi.detach();

        std::cout << "NTP" << std::endl;

        while (true)
        {
            drawUI();
            processMode();
            waitForButtonPress();
            update_wifi_ssid_from_nmcli();
            Read_gps_gnss();//For testing purposes checking in a while loop 
        }
    }
    std::string execCommand(const char *cmd)
    {
        std::array<char, 128> buffer;
        std::string result;

        FILE *pipe = popen(cmd, "r");
        if (!pipe)
            return "";

        while (fgets(buffer.data(), buffer.size(), pipe) != nullptr)
        {
            result += buffer.data();
        }

        pclose(pipe);
        return result;
    }

    void update_wifi_ssid_from_nmcli()
    {
        std::string netType = getActiveNetworkType();

        current_state.wifi_connected = (netType == "wifi");

        if (!current_state.wifi_connected)
        {
            printf("Not connected via Wi-Fi. Current network type: %s\n", netType.c_str());
            return;
        }

        // Get active Wi-Fi SSID using nmcli
        std::string ssid = execCommand("nmcli -t -f active,ssid dev wifi | grep '^yes' | cut -d: -f2");

        // Trim newline if present
        ssid.erase(std::remove(ssid.begin(), ssid.end(), '\n'), ssid.end());

        if (!ssid.empty())
        {
            current_state.wifi_ssid = ssid;
            printf("Connected Wi-Fi SSID (via nmcli): %s\n", ssid.c_str());
        }
        else
        {
            printf("Error: Could not retrieve SSID via nmcli.\n");
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

    std::vector<std::string> getMP4Files(const std::string &dir)
    {

        std::vector<std::string> files;
        for (const auto &entry : fs::directory_iterator(dir))
        {
            if (entry.path().extension() == ".mp4")
            {
                files.push_back(entry.path().string());
            }
        }
        return files;
    }

    bool runFlashlightCommand(const std::vector<std::string> &files)
    {
        if (files.empty())
            return false;

        std::string cmd = "/usr/bin/Flashlight NAK h264";
        for (const auto &f : files)
        {
            cmd += " " + f; // Add each file path to the command
        }

        int result = system(cmd.c_str()); // Run the command using system()
        return (result == 0);             // Return true if the command was successful
    }

    void deleteFiles(const std::vector<std::string> &files)
    {
        for (const auto &f : files)
        {
            std::error_code ec;
            fs::remove(f, ec);
            if (ec)
            {
                std::cerr << "Failed to delete: " << f << " - " << ec.message() << "\n";
            }
            else
            {
                std::cout << "Deleted: " << f << "\n";
            }
        }
    }

    void Local_KVS()
    {

        const std::string videoDir = "/home/local";

        while (true)
        {
            std::string netType = getActiveNetworkType();

            if (netType == "wifi" && !videoRunning)
            {

                auto files = getMP4Files(videoDir);

                if (!files.empty())
                {
                    std::cout << "[Info] Found " << files.size() << " MP4 file(s). Streaming...\n";

                    bool success = runFlashlightCommand(files);

                    if (success)
                    {
                        std::cout << "[Success] Streaming done. Deleting files...\n";
                        std::this_thread::sleep_for(std::chrono::seconds(120));
                        deleteFiles(files);
                    }
                    else
                    {
                        std::cerr << "[Error] Streaming failed. Files kept.\n";
                    }
                }
                else
                {
                    std::cout << "[Info] No files found.\n";
                }
            }
            else
            {
                std::cout << "[Info] Not on Wi-Fi. Skipping streaming.\n";
            }

            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }

    void trackInactivity()
    {

        struct timespec lastActivityTime;
        clock_gettime(CLOCK_MONOTONIC, &lastActivityTime);

        while (true)
        {
            if (activityDetected.load())
            {
                clock_gettime(CLOCK_MONOTONIC, &lastActivityTime);
                activityDetected.store(false);
            }
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            double elapsed = (now.tv_sec - lastActivityTime.tv_sec) +
                             (now.tv_nsec - lastActivityTime.tv_nsec) / 1e9;
            if (elapsed >= 30.0)
            {
                clock_gettime(CLOCK_MONOTONIC, &lastActivityTime); // reset after entering low power
                setColor(0, 0, 0);                                 // Black background
                fillScreen();
                Enter_Power_Mode();
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }

    void drawBatteryAndSignalIcons()
    {
        drawImage(5, 7, battery_level2, 16, 16);
        drawImage(55, 7, signal_level1, 19, 16);
    }

    void drawTimeText(const char *time, int x, int y)
    {
        setColor(31, 63, 31); // Green text
        setbgColor(0, 0, 0);  // Black background
        setFont(ter_u12b);
        drawText(x, y, time);
        printf("Displayed Time on Screen: %s\n", time);
    }
    void drawUI()
    {
        setColor(0, 0, 0); // Black background

        ImageSize modeImages[2] = {
            {RECORD_SCREEN, 80, 60},
            {EMERGENCY_SCREEN, 80, 60}};

        switch (currentState)
        {
        case IDLE:
        {
            drawBatteryAndSignalIcons();
            setColor(31, 63, 31);
            setbgColor(0, 0, 0);
            setFont(ter_u16b);
            drawText(10, 80, currentTime.c_str());
            printf("Displayed Time on Screen: %s\n", currentTime.c_str());
            break;
        }

        case RECORD:
        {
            drawBatteryAndSignalIcons();
            drawImage(0, 60, modeImages[0].image, modeImages[0].width, modeImages[0].height);
            drawTimeText(currentTime.c_str(), 25, 140);
            break;
        }

        case EMERGENCY:
        {
            drawBatteryAndSignalIcons();
            drawImage(0, 60, modeImages[1].image, modeImages[1].width, modeImages[1].height);
            drawTimeText(currentTime.c_str(), 25, 140);
            break;
        }

        case BARCODE:
        {
            setColor(255, 255, 255); // White background
            drawImage(10, 10, barcode, IMAGE_WIDTH, IMAGE_HEIGHT);
            break;
        }

        default:
            // Optionally handle unexpected states
            break;
        }

        // if (videoRunning)
        // {
        //     setColor(31, 63, 31);
        //     setbgColor(0, 0, 0);
        //     setFont(ter_u12b);
        //     drawText(25, 125, videoTime);
        //     printf("videoRunning: %s\n", videoTime);
        // }

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

    int Read_gps_gnss()
    {

        setLineValue(testGpioReq.gps_pwr_en, GPIO_LINE_GPS_PWR_EN, GPIOD_LINE_VALUE_ACTIVE);

        _Delay(100);

        int fd = gps_i2c_init("/dev/i2c-2");
        if (fd < 0)
        {
            printf("Failed to init GPS\n");
            return 1;
        }

        double lat, lon;
        if (gps_get_location(fd, &lat, &lon) == 0)
        {
            printf("Latitude: %.6f, Longitude: %.6f\n", lat, lon);
        }
        else
        {
            printf("Failed to get location\n");
        }

        setLineValue(testGpioReq.gps_pwr_en, GPIO_LINE_GPS_PWR_EN, GPIOD_LINE_VALUE_ACTIVE);

        gps_i2c_close(fd);
    }

    void Enter_Power_Mode()
    {
        if (!videoRunning && !barcode_show)
        {

            const std::string power_state_file = "/sys/power/state";
            // Open the file for writing
            std::ofstream power_state_stream(power_state_file);
            // Check if the file was opened successfully
            if (!power_state_stream.is_open())
            {
                std::cerr << "Error: Unable to open " << power_state_file << std::endl;
                return;
            }
            // Write the "mem" value to trigger suspend-to-RAM
            power_state_stream << "mem" << std::endl;
            // Close the file after writing
            power_state_stream.close();
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
            activityDetected.store(true);
            updateMode(pressed);
        }
        _Delay(5000); // Assuming microseconds (5ms)
    }
    void updateMode(int btn)
    {
        std::lock_guard<std::mutex> lock(state_mutex); // Lock shared state access

        switch (btn)
        {

        case 1: // Mode cycle button
            mode = (mode + 1) % 2;

            if (current_state.in_emergency)
            {
                mode = 0;
                current_state.in_emergency = false;
                currentState = IDLE;
            }

            if (barcode_show)
            {
                setOrientation(R90);
                barcode_show = false;
                currentState = IDLE;
                system("pkill -2 -f /opt/ble_wifi_onboarding/main.py");
            }

            currentState = (mode == 1) ? RECORD : IDLE;
            break;

        case 2: // Emergency button
            if (!current_state.in_emergency)
            {
                currentState = EMERGENCY;
                current_state.in_emergency = true;
            }
            else
            {
                mode = 0;
                currentState = IDLE;
                current_state.in_emergency = false;
            }
            break;

        case 3: // Show barcode
            setOrientation(R180);
            barcode_show = true;
            currentState = BARCODE;
            break;

        default:
            // No action for other button values
            break;
        }

        mark_state_dirty();
        mode_change_time = time(NULL); // Record mode change time
    }

    void processMode()
    {
        std::lock_guard<std::mutex> lock(state_mutex); // Lock shared state access

        if (!current_state.in_emergency && !barcode_show)
        {
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
                lightOn();
                videoOn();
                voipOff();
            }
            //< Below modes can be used in case config changes through the shadow update and we can switch the mode accordingly
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

            activityDetected.store(true);
            mark_state_dirty();
        }
        else if (current_state.in_emergency)
        {
            videoOff();
            lightOn();
            alarmOn();
            emergency_stream_on();
            voipOn();
            mark_state_dirty();
            activityDetected.store(true);
        }
        else if (currentState == BARCODE)
        {
            system("systemctl restart bt-manager");
            usleep(2000000); // let hci0 come up
            system("python3 /opt/ble_wifi_onboarding/main.py &");
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
        testGpioReq.gps_pwr_en = requestOutputLine(GPIO_DEVICE4, GPIO_LINE_GPS_PWR_EN, "GPS POWER ENABLE");
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
                    execle("/usr/bin/Flashlight", "Flashlight", "NAK", "h264", NULL, environ);
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
        buzzer_running.store(false);
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
            printf("Stopping Streaming (PID: %d)\n", gst_pid);

            // Send SIGTERM to gracefully terminate the process
            if (kill(gst_pid, SIGTERM) == 0)
            {
                printf("Sent SIGTERM to process %d\n", gst_pid);
            }
            else
            {
                perror("Failed to send SIGTERM");
            }

            // Wait for up to 3 seconds for the process to terminate
            int status;
            pid_t result;
            int wait_time = 0;
            const int max_wait_time = 3; // seconds

            do
            {
                result = waitpid(gst_pid, &status, WNOHANG);
                if (result == 0)
                {
                    sleep(1);
                    wait_time++;
                }
            } while (result == 0 && wait_time < max_wait_time);

            if (result == gst_pid)
            {
                if (WIFEXITED(status))
                {
                    printf("Flashlight stopped successfully with exit code %d\n", WEXITSTATUS(status));
                }
                else if (WIFSIGNALED(status))
                {
                    printf("Flashlight process terminated by signal %d\n", WTERMSIG(status));
                }
                else
                {
                    printf("Flashlight process exited abnormally\n");
                }
            }
            else if (result == 0)
            {
                // Still running after wait time: force kill
                printf("Process did not exit in time, force killing...\n");
                if (kill(gst_pid, SIGKILL) == 0)
                {
                    printf("Sent SIGKILL to process %d\n", gst_pid);
                    waitpid(gst_pid, &status, 0); // Ensure it's reaped
                }
                else
                {
                    perror("Failed to send SIGKILL");
                }
            }
            else
            {
                perror("Failed to wait for the process");
            }

            // Reset state
            gst_pid = -1;
            videoRunning = 0;
            videoStart = 0;
            sprintf(videoTime, "%02d:%02d", 0, 0);
        }
        else
        {
            printf("No stream running to stop\n");
        }
    }

    std::string getActiveNetworkType()
    {

        std::ifstream file("/run/net_status.flag");
        std::string network;

        if (file.is_open())
        {
            std::getline(file, network);
            file.close();
        }
        else
        {
            network = "unknown"; // Optional fallback if the file isn't found
        }

        return network; // Returns: "wifi", "lte", or "unknown"
    }

    void voipOff()
    {
        printf("voipOff\r\n");
    }

    void alarmOn()
    {
        std::lock_guard<std::mutex> lock(buzzer_mutex);

        printf("alarmOn\r\n");
        if (!buzzer_running.load())
        {
            current_state.alarm_on = true;
            buzzer_running.store(true);
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
            std::string netType = getActiveNetworkType();

            if (netType == "wifi")
            {

                emergency_stream_on();
                return;
            }

            if (gst_pid == -1)
            {
                gst_pid = fork();
                if (gst_pid == 0)
                {
                    videoRunning = 1;
                    videoStart = time(NULL);
                    // Child process: replace this process with the streaming app
                    execle("/usr/bin/Flashlight", "Flashlight", "NAK", "h264", "local_storage", nullptr, environ);
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
            std::lock_guard<std::mutex> lock(buzzer_mutex);
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
                std::this_thread::sleep_for(std::chrono::minutes(1)); // Update every minute
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
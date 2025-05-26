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
enum class StreamAction
{
    Start,
    Stop
};

using json = nlohmann::json;
extern char **environ;
std::mutex state_mutex;
std::atomic<bool> state_dirty = false;
std::atomic<bool> running = true;

std::thread buzzer_thread;
std::atomic<bool> buzzer_running = false;
std::atomic<int> maxtriesreach = 0;
std::atomic<bool> video_run = false;
std::atomic<int> buzzer_frequency_hz;
std::mutex buzzer_mutex;
std::mutex actionMutex;
std::condition_variable actionCV;
std::optional<StreamAction> pendingAction;
std::atomic<bool> streamStartSuccess{false};
std::atomic<bool> streamStopSuccess{false};
std::string lastSeenVersion = "";
namespace fs = std::filesystem;
std::string g_device_name = "TF004"; // Default fallback value
std::string playTestTone = "";
bool flashlightStatus = false;
const std::string shadowPath = "/etc/aws_iot_device/shadow-output.json";
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
time_t videoStart_check1 = 0;
Mode current_mode = STREAM;
std::string currentTime = "00:00"; // Default Time

volatile int videoRunning = 0;
time_t videoStart = 0;
char videoTime[6] = "00:00"; // Default Time
std::atomic<bool> activityDetected{false};
std::thread inactivityThread;
std::thread Stream_Wifi;
std::thread ReadGPs;
bool notifyStartSent = false;
time_t videoStart_check = 0;

time_t videoStopTime = 0;
bool notifyStopSent = false;
std::thread checkwifi;
size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t totalSize = size * nmemb;
    std::string *out = static_cast<std::string *>(userp);
    out->append(static_cast<char *>(contents), totalSize);
    return totalSize;
}
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
        ReadGPs = std::thread(&DisplayExample::GET_GPS_DATA, this);
        ReadGPs.detach();

        checkwifi = std::thread(&DisplayExample::update_wifi_ssid_from_nmcli, this);
        checkwifi.detach();

        std::thread notifier(&DisplayExample::streamNotifierLoop, this);
        notifier.detach();

        std::cout << "NTP" << std::endl;

        if (!load_device_name("/etc/aws_iot_device/config.json"))
        {
            // Handle error
        }

        while (true)
        {
            drawUI();
            processMode();
            waitForButtonPress();
        }
    }
    bool load_device_name(const std::string &config_path)
    {
        std::ifstream file(config_path);
        if (!file.is_open())
        {
            std::cerr << "[Config] Failed to open config file: " << config_path << "\n";
            return false;
        }

        json config_json;
        try
        {
            file >> config_json;
        }
        catch (const std::exception &e)
        {
            std::cerr << "[Config] Failed to parse JSON: " << e.what() << "\n";
            return false;
        }

        if (config_json.contains("thing-name") && config_json["thing-name"].is_string())
        {
            g_device_name = config_json["thing-name"];
            return true;
        }
        else
        {
            std::cerr << "[Config] 'thing-name' not found or invalid\n";
            return false;
        }
    }
    std::string get_stream_url(StreamAction action)
    {
        std::string base_url = "https://api.rolex.mytimeli.com/stream/";
        std::string action_str = (action == StreamAction::Start) ? "start" : "stop";
        return base_url + g_device_name + "/" + action_str;
    }

    void notifyStream(StreamAction action)
    {
        const int max_retries = 5;
        int attempt = 0;
        bool success = false;

        std::string url = get_stream_url(StreamAction::Start);
        std::string jsonData = R"({"codec":"H264","resolution":"1920x1080"})";

        while (attempt < max_retries && !success)
        {
            maxtriesreach.store(max_retries);

            CURL *curl = curl_easy_init();
            if (!curl)
            {
                std::cerr << "Failed to initialize CURL." << std::endl;
                break;
            }

            std::string response_string;
            struct curl_slist *headers = nullptr;
            headers = curl_slist_append(headers, "Content-Type: application/json");

            curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
            if (action == StreamAction::Start)
            {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
            }

            CURLcode res = curl_easy_perform(curl);
            if (res != CURLE_OK)
            {
                std::cerr << ((action == StreamAction::Start) ? "Start" : "Stop")
                          << " stream request failed on attempt " << (attempt + 1)
                          << ": " << curl_easy_strerror(res) << std::endl;
            }
            else
            {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                std::cout << "HTTP Status: " << http_code << std::endl;
                std::cout << "Response: " << response_string << std::endl;

                if (http_code == 200)
                {
                    std::cout << ((action == StreamAction::Start) ? "Start" : "Stop")
                              << " stream notification sent successfully." << std::endl;
                    success = true;
                }
                else
                {
                    std::cerr << ((action == StreamAction::Start) ? "Start" : "Stop")
                              << " stream failed with HTTP status " << http_code
                              << " on attempt " << (attempt + 1) << "." << std::endl;
                }
            }

            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);

            if (!success)
            {
                attempt++;
                if (attempt < max_retries)
                {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                }
            }
        }

        if (action == StreamAction::Start)
            streamStartSuccess.store(success);
        else
            streamStopSuccess.store(success);
    }

    // === Notification thread loop ===
    void streamNotifierLoop()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(actionMutex);
            actionCV.wait(lock, []
                          { return pendingAction.has_value(); });

            StreamAction action = *pendingAction;
            pendingAction.reset(); // clear the action so we don't re-run it

            lock.unlock();

            notifyStream(action);
        }
    }

    // === Main thread signals new action ===
    void signalStreamAction(StreamAction action)
    {
        std::lock_guard<std::mutex> lock(actionMutex);
        pendingAction = action;
        actionCV.notify_one();
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
        while (1)
        {
            std::string netType = getActiveNetworkType();

            current_state.wifi_connected = (netType == "wifi");

            if (!current_state.wifi_connected)
            {
                printf("Not connected via Wi-Fi. Current network type: %s\n", netType.c_str());
                // Assume LTE connection
                if (netType == "lte")
                {
                    current_state.cellular_connected = true;
                }
                std::this_thread::sleep_for(std::chrono::seconds(20));
                continue;
            }

            // Get active Wi-Fi SSID using nmcli
            std::string ssid = execCommand("nmcli -t -f active,ssid dev wifi | grep '^yes' | cut -d: -f2");

            // Trim newline if present
            ssid.erase(std::remove(ssid.begin(), ssid.end(), '\n'), ssid.end());

            if (!ssid.empty())
            {
                current_state.wifi_ssid = ssid;
                printf("Connected Wi-Fi SSID (via nmcli): %s\n", ssid.c_str());
                std::string wifiStrength = execCommand("nmcli -t -f active,signal dev wifi | grep '^yes' | cut -d: -f2");
                wifiStrength.erase(std::remove(wifiStrength.begin(), wifiStrength.end(), '\n'), wifiStrength.end());
                current_state.wifi_strength = wifiStrength;
                printf("Wi-Fi signal strength: %s%%\n", wifiStrength.c_str());
            }
            else
            {
                printf("Error: Could not retrieve SSID via nmcli.\n");
            }

            std::this_thread::sleep_for(std::chrono::seconds(20));
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

        std::string cmd = "/usr/bin/Flashlight TF004 h264";
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

            if (netType == "wifi" && !video_run.load())
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

            std::this_thread::sleep_for(std::chrono::seconds(60));
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
                // Enter_Power_Mode();
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
            {EMERGENCY_SCREEN, 80, 60}

        };

        switch (currentState)
        {
        case IDLE:
        {

            drawBatteryAndSignalIcons();
            setColor(31, 63, 31);
            setbgColor(0, 0, 0);
            setFont(ter_u16b);
            drawText(12, 80, currentTime.c_str());

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
            monitorShadowOutput(shadowPath, playTestTone, flashlightStatus);
            if (state_dirty.load())
            {
                update_shadow_json();
                state_dirty = false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1)); // configurable delay
        }
    }

    void GET_GPS_DATA()
    {
        while (1)
        {

            setLineValue(testGpioReq.gps_pwr_en, GPIO_LINE_GPS_PWR_EN, GPIOD_LINE_VALUE_ACTIVE);

            usleep(4000000); //<4 sec delay
            int fd = gps_i2c_init("/dev/i2c-2");
            if (fd < 0)
            {
                fprintf(stderr, "Failed to initialize GPS\n");
                sleep(30);
                continue;
            }

            double lat = 0.0, lon = 0.0;
            if (gps_get_location(fd, &lat, &lon) == 0)
            {
                printf("Latitude: %.6f, Longitude: %.6f\n", lat, lon);
                if (lat >= -90.0 && lat <= 90.0 &&
                    lon >= -180.0 && lon <= 180.0 &&
                    (fabs(lat) >= 0.05 && fabs(lon) >= 0.05)) // filter small noise
                {
                    current_state.gps_latitude = lat;
                    current_state.gps_longitude = lon;
                }
                else
                {
                    fprintf(stderr, "Ignored invalid or low-accuracy GPS values: %.6f, %.6f\n", lat, lon);
                }
            }
            else
            {
                fprintf(stderr, "Failed to get valid GPS location\n");
            }

            gps_i2c_close(fd);
            usleep(100000);

            std::this_thread::sleep_for(std::chrono::seconds(60)); // Update every minute
        }
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
        printf("Update json\n");

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
            else
            {
                // Ensure correct permissions (0600)
                if (chmod(final_path.c_str(), S_IRUSR | S_IWUSR) != 0)
                {
                    std::cerr << "[Shadow] Failed to set permissions on shadow file\n";
                }
            }
        }
        else
        {
            std::cerr << "[Shadow] Failed to open temp shadow file\n";
        }
    }

    void monitorShadowOutput(std::string shadowFilePath,
                             std::string &playTestTone,
                             bool &flashlightStatus)
    {
        if (!std::filesystem::exists(shadowFilePath))
        {
            std::cerr << "Shadow output file not found: " << shadowFilePath << std::endl;
            return;
        }

        std::ifstream file(shadowFilePath);
        if (!file.is_open())
        {
            std::cerr << "Failed to open shadow output file!" << std::endl;
            return;
        }

        json shadowJson;
        try
        {
            file >> shadowJson;
        }
        catch (const std::exception &e)
        {
            std::cerr << "Failed to parse shadow JSON: " << e.what() << std::endl;
            return;
        }

        // Optional version-based change detection
        std::string versionStr = std::to_string(shadowJson.value("version", 0));
        if (versionStr == lastSeenVersion)
        {
            return; // No update
        }
        lastSeenVersion = versionStr;

        if (!shadowJson.contains("state") || !shadowJson["state"].contains("desired"))
        {
            return;
        }

        auto desired = shadowJson["state"]["desired"];

        if (desired.contains("tone"))
        {
            playTestTone = desired["tone"];
            std::cout << "Received tone command: " << playTestTone << std::endl;
        }

        if (desired.contains("flashlight"))
        {
            flashlightStatus = desired["flashlight"];
            std::cout << "Flashlight status: " << (flashlightStatus ? "ON" : "OFF") << std::endl;
        }
        
        if (flashlightStatus)
        {
            lightOn();
        }
        else
        {
            lightOff();
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
            usleep(5000); // 10ms sleep to avoid CPU hogging
        }
        printf("areButtonsPressed %d\r\n", pressed);
        if (pressed > 0)
        {
            activityDetected.store(true);
            updateMode(pressed);
            mark_state_dirty();
        }
        _Delay(2000); // Assuming microseconds (5ms)
    }
    void updateMode(int btn)
    {
        std::lock_guard<std::mutex> lock(state_mutex); // Lock shared state access
        mark_state_dirty();
        switch (btn)
        {

        case 1: // Mode cycle button

            if (current_state.in_emergency)
            {
                mode = 0;
                current_state.in_emergency = false;
                currentState = IDLE;
                setColor(0, 0, 0); // Black background
                filledRect(0, 0, WIDTH, HEIGHT);
            }

            if (barcode_show)
            {
                setOrientation(R90);
                barcode_show = false;
                currentState = IDLE;
                system("pkill -2 -f /opt/ble_wifi_onboarding/main.py");
                setColor(0, 0, 0); // Black background
                filledRect(0, 0, WIDTH, HEIGHT);
            }
            else
            {
                mode = (mode + 1) % 2;
                currentState = (mode == 1) ? RECORD : IDLE;
                setColor(0, 0, 0); // Black background
                filledRect(0, 0, WIDTH, HEIGHT);
            }
            break;

        case 2: // Emergency button
            if (!current_state.in_emergency)
            {
                currentState = EMERGENCY;
                current_state.in_emergency = true;
                setColor(0, 0, 0); // Black background
                filledRect(0, 0, WIDTH, HEIGHT);
            }
            else
            {
                mode = 0;
                currentState = IDLE;
                current_state.in_emergency = false;
                setColor(0, 0, 0); // Black background
                filledRect(0, 0, WIDTH, HEIGHT);
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

        mode_change_time = time(NULL); // Record mode change time
    }

    void processMode()
    {

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
                std::string netType = getActiveNetworkType();
                if (netType == "wifi")
                {
                    emergency_stream_on();
                }
                else
                {
                    videoOn();
                }
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
        }
        else if (current_state.in_emergency)
        {
            videoOff();
            lightOn();
            alarmOn();
            emergency_stream_on();
            voipOn();
            activityDetected.store(true);
        }
        else if (currentState == BARCODE)
        {
            system("systemctl restart bt-manager");
            usleep(2000000); // let hci0 come up
            system("python3 /opt/ble_wifi_onboarding/main.py &");
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
        video_run.store(true);

        printf("Stream On\r\n");

        if (!videoRunning)
        {
            if (gst_pid == -1)
            {
                gst_pid = fork();
                if (gst_pid == 0)
                {
                    const char *device_name_cstr = g_device_name.c_str();
                    std::cout << "[DEBUG] execle args: /usr/bin/Flashlight "
                              << device_name_cstr << " h264" << std::endl;
                    // Child process: replace this process with the streaming app
                    execle("/usr/bin/Flashlight", // Path to binary
                           "Flashlight",          // argv[0]
                           device_name_cstr,      // argv[1] — dynamic device name
                           "h264",                // argv[2]
                           nullptr,               // End of args
                           environ);
                    perror("execl failed");
                    _exit(1); // In case exec fails
                }
                else
                {
                    videoStart_check1 = time(NULL);
                    videoRunning = 1;
                    notifyStartSent = false; // reset for delayed notify
                    printf("Started GStreamer process with PID: %d\n", gst_pid);
                }
            }
            else
            {
                printf("Stream already running (PID: %d)\n", gst_pid);
            }
        }

        // After stream has started, wait for 10 seconds before notifying
        if (videoRunning && !notifyStartSent && videoStart_check1 != 0)
        {
            time_t now = time(NULL);
            if (difftime(now, videoStart_check) >= 10)
            {
                if (streamStartSuccess.load())
                {
                    std::cout << "Start notification successful.\n";
                    notifyStartSent = true;
                    streamStartSuccess.store(false);
                    maxtriesreach.store(0);
                    videoStart_check1 = 0;
                    return;
                }
                else if (maxtriesreach.load() == 0)
                {
                    signalStreamAction(StreamAction::Start);
                }

                else if (maxtriesreach.load() >= 4)
                {
                    printf("Maximum retries Reached \n");
                    notifyStartSent = true;
                    streamStartSuccess.store(false);
                    maxtriesreach.store(0);
                    videoStart_check1 = 0;
                }
            }
        }
    }

    void alarmOff()
    {
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

            if (kill(gst_pid, SIGTERM) == 0)
            {
                printf("Sent SIGTERM to process %d\n", gst_pid);
            }
            else
            {
                perror("Failed to send SIGTERM");
            }

            int status;
            pid_t result;
            int wait_time = 0;
            const int max_wait_time = 3;

            do
            {
                result = waitpid(gst_pid, &status, WNOHANG);
                if (result == 0)
                {
                    sleep(1);
                    wait_time++;
                }
            } while (result == 0 && wait_time < max_wait_time);

            if (result == gst_pid || result == 0)
            {
                // Record stop time
                videoStopTime = time(NULL);
                notifyStopSent = false;

                // Reap if still running
                if (result == 0)
                {
                    printf("Force killing unresponsive process...\n");
                    if (kill(gst_pid, SIGKILL) == 0)
                    {
                        waitpid(gst_pid, &status, 0);
                        printf("Sent SIGKILL to process %d\n", gst_pid);
                    }
                    else
                    {
                        perror("Failed to send SIGKILL");
                    }
                }

                gst_pid = -1;
                videoRunning = 0;
                video_run.store(false);
                videoStart = 0;
                sprintf(videoTime, "%02d:%02d", 0, 0);
            }
            else
            {
                perror("Failed to wait for the process");
            }
        }

        // Handle deferred notifyStopStream (after 10s)
        if (!videoRunning && !notifyStopSent && videoStopTime != 0)
        {
            time_t now = time(NULL);
            if (difftime(now, videoStopTime) >= 10)
            {
                if (streamStopSuccess.load())
                {
                    notifyStopSent = true;
                    streamStopSuccess.store(false);
                    videoStopTime = 0;
                    return;
                }
                else if (maxtriesreach.load() == 0)
                {
                    signalStreamAction(StreamAction::Stop);
                }

                else if (maxtriesreach.load() >= 4)
                {
                    printf("Maximum retries Reached \n");
                    notifyStopSent = true;
                    streamStopSuccess.store(false);
                    maxtriesreach.store(0);
                    videoStopTime = 0;
                }
            }
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
        video_run.store(true);

        printf("videoOn\r\n");

        if (!videoRunning)
        {
            if (gst_pid == -1)
            {
                gst_pid = fork();
                if (gst_pid == 0)
                {
                    // Child process: replace this process with the streaming app
                    const char *device_name_cstr = g_device_name.c_str();

                    execle("/usr/bin/Flashlight", // Path to executable
                           "Flashlight",          // argv[0]
                           device_name_cstr,      // argv[1] — dynamic device name
                           "h264",                // argv[2]
                           "local_storage",       // argv[3]
                           nullptr,               // end of args
                           environ);              // environment variables
                    perror("execl failed");
                    _exit(1); // In case exec fails
                }
                else
                {
                    videoRunning = 1;
                    videoStart_check = time(NULL);
                    notifyStartSent = false; // Reset the flag
                    printf("Started GStreamer process with PID: %d\n", gst_pid);
                }
            }
            else
            {
                printf("Stream already running (PID: %d)\n", gst_pid);
            }
        }
        // Outside the start condition: check if it's time to notify
        if (videoRunning && !notifyStartSent && videoStart_check != 0)
        {
            time_t now = time(NULL);
            if (difftime(now, videoStart_check) >= 10)
            {
                if (streamStartSuccess.load())
                {
                    std::cout << "Start notification successful.\n";
                    notifyStartSent = true;
                    streamStartSuccess.store(false);
                    maxtriesreach.store(0);
                    videoStart_check = 0;
                    return;
                }
                else if (maxtriesreach.load() == 0)
                {
                    signalStreamAction(StreamAction::Start);
                }

                else if (maxtriesreach.load() >= 4)
                {
                    printf("Maximum retries Reached \n");
                    notifyStartSent = true;
                    streamStartSuccess.store(false);
                    maxtriesreach.store(0);
                    videoStart_check = 0;
                }
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
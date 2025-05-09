#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <gpiod.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <linux/input.h>
#include "appgpio.h"
#include <poll.h>

#define PAGE_SIZE 4096 // Typical page size on ARM
#define PAGE_MASK (PAGE_SIZE - 1)

#define TARGET_DEVICE_NAME "gpio-keys"
#define EVENT_DEV_PATH "/dev/input/"
#define DEBOUNCE_DELAY_US 20000 // 20ms
#define POLL_TIMEOUT_MS 50
#define CHECK_TIMEOUT_S 1.0

enum ButtonState
{
    BUTTON_NONE = 0,
    BUTTON_EVENT_ONLY = 1,
    BUTTON_GPIO_ONLY = 2,
    BUTTON_BOTH = 3,
    ERROR_DEVICE_NOT_FOUND = -1,
    ERROR_GPIO_NOT_INIT = -2,
    ERROR_TIMEOUT = -3,
    ERROR_READ_FAIL = -4
};
// Simple delay function (if not already defined)
void _Delay(int microseconds)
{
    usleep(microseconds);
}
char *find_event_device(const char *target_name)
{
    static char event_path[256];
    FILE *fp = fopen("/proc/bus/input/devices", "r");
    if (!fp)
    {
        perror("Failed to open /proc/bus/input/devices");
        return NULL;
    }

    char line[512];
    int found = 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "N: Name=", 8) == 0)
        {
            // Match device name
            if (strstr(line, target_name))
            {
                found = 1;
            }
            else
            {
                found = 0;
            }
        }

        // After matching name, look for event handler
        if (found && strncmp(line, "H: Handlers=", 12) == 0)
        {
            char *pos = strstr(line, "event");
            if (pos)
            {
                char event_name[32];
                if (sscanf(pos, "%31s", event_name) == 1)
                {
                    snprintf(event_path, sizeof(event_path), "%s%s", EVENT_DEV_PATH, event_name);
                    fclose(fp);
                    return event_path;
                }
            }
        }
    }

    fclose(fp);
    return NULL;
}

// Function to simulate the requestOutputLine (you should implement it according to your system)
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer)
{
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();

    if (!req_cfg || !settings || !line_cfg)
    {
        printf("Failed to allocate GPIO settings");
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_PUSH_PULL);
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    gpiod_request_config_set_consumer(req_cfg, consumer);

    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (!chip)
    {
        printf("Failed to open GPIO chip");
    }

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    gpiod_chip_close(chip);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_request_config_free(req_cfg);

    if (!request)
    {
        printf("Failed to request GPIO line %d", offset);
    }

    return request;
}

void setLineValue(struct gpiod_line_request *request, unsigned int line_offset, enum gpiod_line_value value)
{
    // std::cout << "line_offset: " << line_offset
    //           << ", rst_line_offset: " << rst_line_offset
    //           << ", dc_offset: " << dc_line_offset << std::endl;

    if (!request || gpiod_line_request_set_value(request, line_offset, value) < 0)
    {
        printf("Failed to set GPIO line value\r\n");
    }
}
// GPIO configuration (adjust as needed)
#define GPIO_CHIP "/dev/gpiochip3" // Full path to GPIO chip
#define GPIO_LINE1 6               // First button GPIO line number
#define GPIO_LINE2 2               // Second button GPIO line number

static struct gpiod_chip *chip = NULL;
static struct gpiod_line_request *line_request = NULL;
static time_t last_trigger_time = 0; // Track last trigger time for debouncing

/**
 * Initialize GPIOs for two buttons
 * @return 0 on success, -1 on failure
 */
int initButtons(void)
{
    chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip)
    {
        perror("Failed to open GPIO chip");
        return -1;
    }

    // Create line settings for input
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings)
    {
        perror("Failed to create settings");
        gpiod_chip_close(chip);
        return -1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

    // Create line config for both GPIO lines
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    if (!line_config)
    {
        perror("Failed to create line config");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    unsigned int offsets[] = {GPIO_LINE2};

    if (gpiod_line_config_add_line_settings(line_config, offsets, 1, settings) < 0)
    {
        perror("Failed to add line settings");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    // Create request config
    struct gpiod_request_config *req_config = gpiod_request_config_new();
    if (!req_config)
    {
        perror("Failed to create request config");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }
    gpiod_request_config_set_consumer(req_config, "button");

    // Request both lines at once
    line_request = gpiod_chip_request_lines(chip, req_config, line_config);
    if (!line_request)
    {
        perror("Failed to request GPIO lines");
        gpiod_request_config_free(req_config);
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    // Clean up temporary objects
    gpiod_request_config_free(req_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);

    return 0;
}

/**
 * Clean up GPIO resources
 */
void cleanupButtons(void)
{
    if (line_request)
    {
        gpiod_line_request_release(line_request);
    }
    if (chip)
    {
        gpiod_chip_close(chip);
    }
}

/**
 * Check if either button is pressed (either GPIO low) with timeout and debounce
 * @return 1 if either pressed, 0 if neither pressed, -1 on error, -2 on timeout
 */
int areButtonsPressed(void)
{
    const char *device_path = find_event_device(TARGET_DEVICE_NAME);
    if (!device_path)
    {
        fprintf(stderr, "Could not find input device for '%s'\n", TARGET_DEVICE_NAME);
        return ERROR_DEVICE_NOT_FOUND;
    }
    printf("Found device path: %s\n", device_path);

    if (!line_request)
    {
        fprintf(stderr, "GPIOs not initialized\n");
        return ERROR_GPIO_NOT_INIT;
    }

    int fd = open(device_path, O_RDONLY | O_NONBLOCK);
    if (fd < 0)
    {
        perror("Failed to open event device");
        return ERROR_READ_FAIL;
    }

    struct input_event ev;
    static struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN};

    while (1)
    {
        // Check elapsed time
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) +
                         (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed >= CHECK_TIMEOUT_S)
        {
            close(fd);
            return ERROR_TIMEOUT;
        }

        // Check GPIO value
        enum gpiod_line_value gpio_val[1]; //<Normal high state
        if (gpiod_line_request_get_values(line_request, gpio_val) < 0)
        {
            perror("Failed to read GPIO values");
            close(fd);
            return ERROR_READ_FAIL;
        }
        int wake_event = 0;
        // Check input event (non-blocking poll)
        int poll_result = poll(&pfd, 1, POLL_TIMEOUT_MS);

        if (poll_result < 0)
        {
            perror("poll failed");
            close(fd);
            return ERROR_READ_FAIL;
        }

        if (poll_result > 0 && (pfd.revents & POLLIN)) {

    
            ssize_t n = read(fd, &ev, sizeof(ev));
            printf("Read returned: %zd bytes\n", n);
    
            if (n == sizeof(ev)) {
                printf("Event: type=%d, code=%d, value=%d\n", ev.type, ev.code, ev.value);
    
                if (ev.type == EV_KEY && ev.code == KEY_WAKEUP) {
                    printf("KEY_WAKEUP detected\n");
                    if (ev.value == 1) {
                        wake_event = 1;
                        printf("Wake event triggered!\n");
                    }
                }
            }
        }

        if (gpio_val[0] == GPIOD_LINE_VALUE_INACTIVE || wake_event)
        {
            time_t now = time(NULL);
            if (now - last_trigger_time >= 1)
            {
                usleep(DEBOUNCE_DELAY_US); // Debounce delay

                // Recheck both inputs
                if (gpiod_line_request_get_values(line_request, gpio_val) < 0)
                {
                    perror("Failed to re-read GPIO values");
                    close(fd);
                    return ERROR_READ_FAIL;
                }

                poll_result = poll(&pfd, 1, POLL_TIMEOUT_MS);
                if (poll_result > 0)
                {
                    ssize_t bytes = read(fd, &ev, sizeof(ev));
                    if (bytes == sizeof(ev))
                    {
                        if (ev.type == EV_KEY && ev.code == KEY_WAKEUP && ev.value == 1)
                        {
                            wake_event = 1;
                        }
                        else
                        {
                            wake_event = 0;
                        }
                    }
                }

                printf("button (GPIO): %d, event: %d\n", gpio_val[0], wake_event);

                last_trigger_time = now;
                close(fd);

                if (gpio_val[0] == GPIOD_LINE_VALUE_INACTIVE && wake_event)
                    return BUTTON_BOTH;
                else if (gpio_val[0] == GPIOD_LINE_VALUE_INACTIVE)
                    return BUTTON_GPIO_ONLY;
                else if (wake_event)
                    return BUTTON_EVENT_ONLY;
                else
                    return BUTTON_NONE;
            }
        }

        usleep(10000); // Sleep 10ms before next check
    }

    close(fd); // Good practice even if never reached
    return BUTTON_NONE;
}

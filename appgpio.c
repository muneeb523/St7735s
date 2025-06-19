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
#include <linux/input-event-codes.h>
#include <sys/epoll.h>
#include <string.h>
#include <errno.h>


#define MAX_EVENTS 5
#define PAGE_SIZE 4096 // Typical page size on ARM
#define PAGE_MASK (PAGE_SIZE - 1)


#define DEBOUNCE_DELAY_US 200 // 20ms
#define POLL_TIMEOUT_MS 100
#define CHECK_TIMEOUT_S 1.0
/*Macros for button presses*/
#define BUTTON_NONE_PRESSED     -2
#define BUTTON_ERROR            -1
#define BUTTON_SHUTDOWN         0
#define BUTTON1_PRESSED         1
#define BUTTON2_PRESSED         2
#define BOTH_BUTTONS_PRESSED    3

#define DEBOUNCE_DELAY_MS       50
#define LONG_PRESS_SECONDS      3.0
#define POLL_TIMEOUT_SECONDS    1.0
#define POLLING_INTERVAL_US     5000

#define CONSUMER "nCHRG_INT_Monitor"
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
#define GPIO_LINE2 3               // Second button GPIO line number
#define GPIO_LINE_14 14
#define CONSUMER_LABEL "nCHRG_INT"

static struct gpiod_chip *chip = NULL;
static struct gpiod_line_request *line_request_button = NULL;
static struct gpiod_line_request *line_requests = NULL;
struct gpiod_line_request *line_request = NULL;
static time_t last_trigger_time = 0; // Track last trigger time for debouncing


int init_battery_charging_pins() {

    chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        perror("Failed to create line settings");
        gpiod_chip_close(chip);
        return -1;
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_FALLING);
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    gpiod_line_settings_set_event_clock(settings, GPIOD_LINE_CLOCK_MONOTONIC);

    struct gpiod_line_config *line_config = gpiod_line_config_new();
    if (!line_config) {
        perror("Failed to create line config");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    unsigned int offsets[] = { GPIO_LINE_14 };
    if (gpiod_line_config_add_line_settings(line_config, offsets, 1, settings) < 0) {
        perror("Failed to add line settings");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    struct gpiod_request_config *req_config = gpiod_request_config_new();
    if (!req_config) {
        perror("Failed to create request config");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    gpiod_request_config_set_consumer(req_config, CONSUMER_LABEL);

    line_request = gpiod_chip_request_lines(chip, req_config, line_config);
    if (!line_request) {
        perror("Failed to request GPIO lines");
        gpiod_request_config_free(req_config);
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    // Clean up config objects
    gpiod_request_config_free(req_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);


    return 0;
}
/**
 * Initialize GPIOs for two buttons
 * @return 0 on success, -1 on failure
 */
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

    unsigned int offsets[] = {GPIO_LINE1, GPIO_LINE2};
    if (gpiod_line_config_add_line_settings(line_config, offsets, 2, settings) < 0)
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
    gpiod_request_config_set_consumer(req_config, "buttons");

    // Request both lines at once
    line_requests = gpiod_chip_request_lines(chip, req_config, line_config);
    if (!line_requests)
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
    if (line_requests)
    {
        gpiod_line_request_release(line_requests);
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
/**
 * Check if either button is pressed (either GPIO low) with timeout and debounce
 * @return 1 if either pressed, 0 if neither pressed, -1 on error, -2 on timeout
 */
int areButtonsPressed(void)
{
    if (!line_requests)
    {
        fprintf(stderr, "GPIOs not initialized\n");
        return BUTTON_ERROR;
    }

    static int button1_held = 0;
    static int button2_held = 0;
    static struct timespec button1_press_time = {0}, button2_press_time = {0};

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    while (1)
    {
        int values[2] = {1, 1}; // Default: both not pressed (active-low)

        if (gpiod_line_request_get_values(line_requests, values) < 0)
        {
            perror("Failed to read GPIO values");
            return BUTTON_ERROR;
        }

        clock_gettime(CLOCK_MONOTONIC, &current_time);

        // Exit if 1 second has elapsed
        double total_elapsed = (current_time.tv_sec - start_time.tv_sec) +
                               (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (total_elapsed >= POLL_TIMEOUT_SECONDS)
            return BUTTON_NONE_PRESSED;

        //
        // Handle BUTTON 1 (Long or Short press)
        //
        if (values[0] == 0) // Pressed (active-low)
        {
            if (!button1_held)
            {
                button1_held = 1;
                clock_gettime(CLOCK_MONOTONIC, &button1_press_time);
            }
            else
            {
                double press_duration = (current_time.tv_sec - button1_press_time.tv_sec) +
                                        (current_time.tv_nsec - button1_press_time.tv_nsec) / 1e9;
                if (press_duration >= LONG_PRESS_SECONDS)
                {
                    button1_held = 0; // reset
                    return BUTTON_SHUTDOWN;
                }
            }
        }
        else if (button1_held) // Released
        {
            button1_held = 0;
            // Debounce release
            usleep(DEBOUNCE_DELAY_MS * 1000);
            return BUTTON1_PRESSED;
        }

        //
        // Handle BUTTON 2 (Short press only)
        //
        if (values[1] == 0)
        {
            if (!button2_held)
            {
                button2_held = 1;
                clock_gettime(CLOCK_MONOTONIC, &button2_press_time);
            }
        }
        else if (button2_held)
        {
            button2_held = 0;
            // Debounce release
            usleep(DEBOUNCE_DELAY_MS * 1000);
            return BUTTON2_PRESSED;
        }

        //
        // Handle both buttons pressed simultaneously
        //
        if (values[0] == 0 && values[1] == 0)
        {
            usleep(DEBOUNCE_DELAY_MS * 1000); // debounce delay
            // Confirm after debounce
            if (gpiod_line_request_get_values(line_requests, values) < 0)
                return BUTTON_ERROR;

            if (values[0] == 0 && values[1] == 0)
                return BOTH_BUTTONS_PRESSED;
        }

        usleep(POLLING_INTERVAL_US); // Polling delay
    }
}
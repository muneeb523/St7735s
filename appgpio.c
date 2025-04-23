#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <gpiod.h>
#include <unistd.h>
#include <time.h>

#include "appgpio.h"

#define PAGE_SIZE 4096       // Typical page size on ARM
#define PAGE_MASK (PAGE_SIZE - 1)

// Simple delay function (if not already defined)
void _Delay(int microseconds) {
    usleep(microseconds);
}


// Function to simulate the requestOutputLine (you should implement it according to your system)
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer)
{
    struct gpiod_request_config *req_cfg = gpiod_request_config_new();
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_cfg = gpiod_line_config_new();

    if (!req_cfg || !settings || !line_cfg) {
        printf("Failed to allocate GPIO settings");
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_drive(settings, GPIOD_LINE_DRIVE_PUSH_PULL);
    gpiod_line_settings_set_bias(settings, GPIOD_LINE_BIAS_PULL_UP);
    gpiod_line_settings_set_output_value(settings, GPIOD_LINE_VALUE_INACTIVE);
    gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings);
    gpiod_request_config_set_consumer(req_cfg, consumer);

    struct gpiod_chip *chip = gpiod_chip_open(chip_path);
    if (!chip) {
        printf("Failed to open GPIO chip");
    }

    struct gpiod_line_request *request = gpiod_chip_request_lines(chip, req_cfg, line_cfg);

    gpiod_chip_close(chip);
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    gpiod_request_config_free(req_cfg);

    if (!request) {
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
#define GPIO_CHIP "/dev/gpiochip3"  // Full path to GPIO chip
#define GPIO_LINE1 6                // First button GPIO line number
#define GPIO_LINE2 2                // Second button GPIO line number

static struct gpiod_chip *chip = NULL;
static struct gpiod_line_request *line_request = NULL;
static time_t last_trigger_time = 0;  // Track last trigger time for debouncing

/**
 * Initialize GPIOs for two buttons
 * @return 0 on success, -1 on failure
 */
int initButtons(void) {
    chip = gpiod_chip_open(GPIO_CHIP);
    if (!chip) {
        perror("Failed to open GPIO chip");
        return -1;
    }

    // Create line settings for input
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    if (!settings) {
        perror("Failed to create settings");
        gpiod_chip_close(chip);
        return -1;
    }
    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);

    // Create line config for both GPIO lines
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    if (!line_config) {
        perror("Failed to create line config");
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    unsigned int offsets[] = {GPIO_LINE1, GPIO_LINE2};
    if (gpiod_line_config_add_line_settings(line_config, offsets, 2, settings) < 0) {
        perror("Failed to add line settings");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }

    // Create request config
    struct gpiod_request_config *req_config = gpiod_request_config_new();
    if (!req_config) {
        perror("Failed to create request config");
        gpiod_line_config_free(line_config);
        gpiod_line_settings_free(settings);
        gpiod_chip_close(chip);
        return -1;
    }
    gpiod_request_config_set_consumer(req_config, "buttons");

    // Request both lines at once
    line_request = gpiod_chip_request_lines(chip, req_config, line_config);
    if (!line_request) {
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
void cleanupButtons(void) {
    if (line_request) {
        gpiod_line_request_release(line_request);
    }
    if (chip) {
        gpiod_chip_close(chip);
    }
}

/**
 * Check if either button is pressed (either GPIO low) with timeout and debounce
 * @return 1 if either pressed, 0 if neither pressed, -1 on error, -2 on timeout
 */
int areButtonsPressed(void) {
    if (!line_request) {
        fprintf(stderr, "GPIOs not initialized\n");
        return -1;
    }

    struct timespec start_time, current_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);  // Start timing

    while (1) {
        int values[2];
        if (gpiod_line_request_get_values(line_request, values) < 0) {
            perror("Failed to read GPIO values");
            return -1;
        }

        // Check elapsed time for timeout (1 second)
        clock_gettime(CLOCK_MONOTONIC, &current_time);
        double elapsed = (current_time.tv_sec - start_time.tv_sec) +
                         (current_time.tv_nsec - start_time.tv_nsec) / 1e9;
        if (elapsed >= 1.0) {
            return -2;  // Timeout after 1 second
        }

        // Check if either button is pressed (active-low)
        if (values[0] == 0 || values[1] == 0) {
            // Debounce: Ensure 1 second has passed since last trigger
            time_t now = time(NULL);
            if (now - last_trigger_time >= 1) {
                // Confirm button state after 20ms debounce delay
                usleep(20000);  // 20ms debounce
                if (gpiod_line_request_get_values(line_request, values) < 0) {
                    return -1;
                }
                if (values[0] == 0 || values[1] == 0) {
                    last_trigger_time = now;  // Update last trigger time
                    printf("buttons %d, %d\r\n", values[0], values[1]);
                    if (values[0] == 0 && values[1] == 0) {
                        return 3;  // Button press confirmed
                    } else if(values[1] == 0) {
                        return 2;  // Button press confirmed
                    } else {
                        return 1;  // Button press confirmed
                    }
                }
            }
        }
        usleep(10000);  // 10ms polling interval
    }
}


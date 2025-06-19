#include <gpiod.h>
#define CHARGING 1
#define NOT_CHARGING 0
#define CHARGING_ERROR -1
void _Delay(int microseconds);
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer);
void setLineValue(struct gpiod_line_request *request, unsigned int line_offset, enum gpiod_line_value value);
int initButtons(void);
int areButtonsPressed(void);
int init_battery_charging_pins();
extern struct gpiod_line_request *line_request ;

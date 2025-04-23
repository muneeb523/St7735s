#include <gpiod.h>

void _Delay(int microseconds);
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer);
void setLineValue(struct gpiod_line_request *request, unsigned int line_offset, enum gpiod_line_value value);
int initButtons(void);
int areButtonsPressed(void);

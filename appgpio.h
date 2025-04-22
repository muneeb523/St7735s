#include <gpiod.h>

void _Delay(int microseconds);
void forceGPIOs( void );
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer);
void setLineValue(struct gpiod_line_request *request, unsigned int line_offset, enum gpiod_line_value value);
int initButtons(void);
int areButtonsPressed(void);
uint32_t read_register(uint32_t phys_addr);
int write_register(uint32_t phys_addr, uint32_t value);
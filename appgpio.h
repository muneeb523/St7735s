#include <gpiod.h>

void _Delay(int microseconds);
struct gpiod_line_request *requestOutputLine(const char *chip_path, unsigned int offset, const char *consumer);
void setLineValue(struct gpiod_line_request *request, unsigned int line_offset, enum gpiod_line_value value);
int initButtons(void);
int areButtonsPressed(void);

if (read(fd, &ev, sizeof(struct input_event)) > 0) {
    if (ev.type == EV_KEY && ev.code == KEY_VOLUMEUP) {

        if (ev.value == 1) {
            printf("Button pressed\n");
        } else if (ev.value == 0) {
            printf("Button released\n");
        }
        
    }
}

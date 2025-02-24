#include <iostream>
#include <chrono>
#include <thread>

extern "C"
{
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
#include "../st7735s_compat.h"
}

enum Mode { CAMERA, SOUND, CALL, TORCH };

struct ImageSize {
    const uint16_t *image_24;
    const uint16_t *image_32;
};

int i = 0;
Mode current_mode = CAMERA;
Mode next_mode = SOUND;

class DisplayExample {
public:
    void run() {
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();
        while (true) {
            drawUI();
            waitForButtonPress();
        }
    }

    void drawUI() {
        setColor(2, 2, 2);
        fillScreen();

        drawImage(5, 12, battery_good, 24, 24);
        drawImage(55, 15, signal, 20, 18);

        struct IconPosition { int x, y; };
        IconPosition positions[4] = {
            {48, 60}, {10, 60}, {45, 95}, {10, 95}
        };

        ImageSize imageSets[4] = {
            {video_16_16, video_32_32},
            {sound_16_16, sound_32_32},
            {flash_16_16, flash_32_32},
            {voip_16_16, voip_32_32}
        };

        // Max highlighting effect for selected modes
        for (int j = 0; j < 4; j++) {
            bool isSelected = (j == current_mode || j == next_mode);
            int iconSize = isSelected ? 40 : 16; // Max pop-up size
            int offset = isSelected ? -12 : 0; // More prominent pop-up effect

            drawImage(positions[j].x, positions[j].y + offset,
                      isSelected ? imageSets[j].image_32 : imageSets[j].image_24,
                      iconSize, iconSize);
        }

        flushBuffer();
    }

    void waitForButtonPress() {
        while (!isButtonPressed()) {}
        updateMode();
        _Delay(200);
    }

    bool isButtonPressed() {
        static int counter = 0;
        counter++;
        return (counter % 500000 == 0); // Faster response to button press
        _Delay(5000);
    }

    void updateMode() {
        if (i == 0) {
            current_mode = CAMERA;
            next_mode = SOUND;
        } else if (i == 1) {
            current_mode = SOUND;
            next_mode = CALL;
        } else if (i == 2) {
            current_mode = CALL;
            next_mode = TORCH;
        } else if (i == 3) {
            current_mode = TORCH;
            next_mode = CAMERA;
            i = -1;
        }
        i++;
    }
};

int main() {
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

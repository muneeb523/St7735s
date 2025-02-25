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

enum Mode { STREAM, BUZZER, VoIP, FLASHLIGHT };

struct ImageSize {
    const uint16_t *image;
    int width;
    int height;
};

int i = 0;
Mode current_mode = STREAM;

class DisplayExample {
public:
    void run() {
        ST7735S_Init();
        setOrientation(R180);
        fillScreen();
        flushBuffer();
        while (i=0) {
            drawUI();
            waitForButtonPress();
        }
        i=1;
    }

    void drawUI() {
        // setColor(0, 0, 0); // Black background
        fillScreen();

        // Battery and Signal icons
        // drawImage(5, 12, battery_level2, 24, 24);
        // drawImage(55, 15, signal_level3, 20, 18);

        // Modes with their respective image names & sizes
        ImageSize modeImages[6] = {
            {Whole, 160, 80},
            {Mode2, 80, 60},
            {Mode3, 80, 60},
            {Mode4, 80, 60},
            {Mode5, 80, 60},
            {Mode6, 80, 60}
        };

        // Draw the complete mode image at a fixed position
        drawImage(0, 0, modeImages[i].image, modeImages[i].width, modeImages[i].height);
        flushBuffer();
    }

    void waitForButtonPress() {
        while (!isButtonPressed()) {}
        updateMode();
        _Delay(5000);
    }

    bool isButtonPressed() {
        static int counter = 0;
        counter++;
        return (counter % 500000 == 0);
        _Delay(10000);
    }

    void updateMode() {
        i = (i + 1) % 6; // Cycle through 6 modes
    }
};

int main() {
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

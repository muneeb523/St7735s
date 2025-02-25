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
        setOrientation(R90);
        fillScreen();
        flushBuffer();
        while (true) {
            drawUI();
            waitForButtonPress();
        }
    }

    void drawUI() {
        setColor(0, 0, 0); // Black background
        fillScreen();

        // Battery and Signal icons
        drawImage(5, 12, battery_good, 24, 24);
        drawImage(55, 15, signal, 20, 18);

        // Modes with their respective image names & sizes
        ImageSize modeImages[6] = {
            {Mode1, 54, 60},
            {Mode2, 60, 54},
            {Mode3, 57, 57},
            {Mode4, 57, 57},
            {Mode5, 56, 57},
            {Mode6, 57, 57}
        };

        // Draw the complete mode image at a fixed position
        drawImage(20, 50, modeImages[i].image, modeImages[i].width, modeImages[i].height);
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

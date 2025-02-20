#include <iostream>

// Ensure C functions can be used in C++
extern "C" {
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
}

// Define modes
enum Mode {
    CAMERA,
    SOUND,
    CALL,
    TORCH
};

Mode current_mode = CAMERA;  // Start with Camera mode

class DisplayExample {
public:
    void run() {
        ST7735S_Init();
        setOrientation(R180);

        while (true) {
            drawUI();  // Refresh UI
            waitForButtonPress(); // Wait for a button press to change mode
        }
    }

    void drawUI() {
        fillScreen();  // Clear screen

        // Define pie wedge angles
        float angles[4][2] = {
            {0, 90},    // CAMERA
            {90, 180},  // SOUND
            {180, 270}, // CALL
            {270, 360}  // TORCH
        };

        // Draw pie wedges dynamically
        for (int i = 0; i < 4; i++) {
            if (i == current_mode) {
                setColor(31, 31, 0);  // Active mode: Bright Yellow (RGB565)
                drawPie(80, 40, 30, angles[i][0], angles[i][1]);  // Bigger wedge
            } else {
                setColor(10, 10, 10);  // Inactive mode: Dark gray
                drawPie(80, 40, 20, angles[i][0], angles[i][1]);  // Smaller wedge
            }
        }

        // Adjust icon positions to be inside the wedges
        int iconX = 70, iconY = 30; // Default position

        switch (current_mode) {
            case CAMERA:
                drawImage(iconX, iconY, cam_on, 28, 28);
                break;
            case SOUND:
                drawImage(iconX, iconY, cam_off, 28, 28);
                break;
            case CALL:
                drawImage(iconX, iconY, mic, 28, 28);
                break;
            case TORCH:
                drawImage(iconX, iconY, cam_off, 28, 28);
                break;
        }

        flushBuffer();  // Update display
    }

    void waitForButtonPress() {
        while (!isButtonPressed()) {} // Wait until button press is detected
        updateMode(); // Change mode when pressed
    }

    bool isButtonPressed() {
        // Mock function - Replace with actual button read logic
        static int counter = 0;
        counter++;
        return (counter % 2000000 == 0); // Simulate button press
    }

    // Change mode when button is pressed
    void updateMode() {
        current_mode = static_cast<Mode>((current_mode + 1) % 4);  // Cycle modes
      //  drawUI();  // Refresh screen after mode change
    }
};

// Main function
int main() {
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

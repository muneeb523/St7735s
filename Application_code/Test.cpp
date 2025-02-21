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
        setOrientation(R180);  // Set display orientation for landscape mode
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

        int centerX = 80;  // Adjusted for landscape mode
        int centerY = 40;
        int radius = 30;   // Reduced radius for better fitting

        // Draw pie wedges dynamically
        for (int i = 0; i < 4; i++) {
            if (i == current_mode) {
                setColor(31, 31, 0);  // Active mode: Bright Yellow (RGB565)
                drawPie(centerX, centerY, radius, angles[i][0], angles[i][1]);
            } else {
                setColor(10, 10, 10);  // Inactive mode: Dark gray
                drawPie(centerX, centerY, radius - 5, angles[i][0], angles[i][1]);
            }
        }

        // Adjusted icon positions for landscape mode
        struct IconPosition {
            int x, y;
        };

        IconPosition positions[4] = {
            {120, 20}, // CAMERA
            {40, 20},  // SOUND
            {40, 50},  // CALL
            {120, 50}  // TORCH
        };

        // Draw correct icon for the current mode
        switch (current_mode) {
            case CAMERA:
                drawImage(positions[0].x, positions[0].y, cam_on, 20, 20);
                break;
            case SOUND:
                drawImage(positions[1].x, positions[1].y, cam_off, 20, 20);
                break;
            case CALL:
                drawImage(positions[2].x, positions[2].y, mic, 20, 20);
                break;
            case TORCH:
                drawImage(positions[3].x, positions[3].y,cam_on, 20, 20);
                break;
        }

        // Draw battery and signal icons at the top
        drawImage(5, 5, battery_icon, 16, 8);
        drawImage(140, 5, signal_icon, 12, 8);

        flushBuffer();  // Update display
    }

    void waitForButtonPress() {
        while (!isButtonPressed()) {}
        updateMode();
    }

    bool isButtonPressed() {
        // Placeholder for real button press logic (use GPIO input if applicable)
        static int counter = 0;
        counter++;
        return (counter % 1000000 == 0); // Simulated press for testing
    }

    void updateMode() {
        current_mode = static_cast<Mode>((current_mode + 1) % 4); // Cycle modes
    }
};

int main() {
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

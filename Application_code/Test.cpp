﻿#include <iostream>

// Ensure C functions can be used in C++
extern "C"
{
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
}
// Define modes
enum Mode
{
    CAMERA,
    SOUND,
    CALL,
    TORCH
};

Mode current_mode = SOUND; // Start with Camera mode
int previous_mode = -1;
class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90); // Set display orientation for landscape mode
        int i = 1;
        while (i)
        {
            drawUI(); // Refresh UI
            i = 0;
              //waitForButtonPress(); // Wait for a button press to change mode
        }
    }

    void drawUI()
    {

        fillScreen(); // Clear screen

        drawImage(5, 12, battery_good, 24, 24);
        drawImage(55, 15, signal, 20, 18);
        flushBuffer();

        // Define pie wedge angles
        float angles[4][2] = {
            {0, 90},    // CAMERA
            {90, 180},  // SOUND
            {180, 270}, // CALL
            {270, 360}  // TORCH
        };

        int centerX = 40; // Adjusted for landscape mode
        int centerY = 80;
        int radius = 32; // Reduced radius for better fitting

        // Draw pie wedges dynamically
        int gapAngle = 10; // Define a small gap between slices

        for (int i = 0; i < 4; i++)
        {
            int startAngle = angles[i][0] + gapAngle / 2; // Add half gap at start
            int endAngle = angles[i][1] - gapAngle / 2;   // Subtract half gap at end

            if (i == current_mode)
            {
                setColor(31, 31, 0); // Active mode: Bright Yellow
                drawPie(centerX, centerY, radius, startAngle, endAngle);
            }
            else
            {
                setColor(10, 10, 10); // Inactive mode: Dark gray
                drawPie(centerX, centerY, radius - 5, startAngle, endAngle);
            }
        }

        flushBuffer();
        // Adjusted icon positions for landscape mode
        struct IconPosition
        {
            int x, y;
        };

        IconPosition positions[4] = {
            {25, 130}, // CAMERA
            {25, 130},  // SOUND
            {40, 50},  // CALL
            {120, 50}  // TORCH
        };

        // Draw correct icon for the current mode
        switch (current_mode)
        {
        case CAMERA:
            drawImage(positions[0].x, positions[0].y, cam_on, 28, 28);
            break;
        case SOUND:
            drawImage(positions[1].x, positions[1].y, mic, 16, 16);
            break;
        case CALL:
            drawImage(positions[2].x, positions[2].y, mic, 27, 27);
            break;
        case TORCH:
            drawImage(positions[3].x, positions[3].y, cam_on, 27, 27);
            break;
        }

        flushBuffer(); // Update display
    }

    void waitForButtonPress()
    {
        while (!isButtonPressed())
        {
        }
        updateMode();
    }

    bool isButtonPressed()
    {
        // Placeholder for real button press logic (use GPIO input if applicable)
        static int counter = 0;
        counter++;
        return (counter % 1000000 == 0); // Simulated press for testing
    }

    void updateMode()
    {
        current_mode = static_cast<Mode>((current_mode + 1) % 4); // Cycle modes
    }
};

int main()
{
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

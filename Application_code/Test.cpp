#include <iostream>

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

Mode current_mode = CAMERA; // Start with Camera mode
int previous_mode = -1;

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90); // Set display orientation for landscape mode

        while (true)
        {
            drawUI(); // Refresh UI
            waitForButtonPress(); // Wait for a button press to change mode
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
        int gapAngle = 10; // Define a small gap between slices

        for (int i = 0; i < 4; i++)
        {
            int startAngle = angles[i][0] + gapAngle / 2;
            int endAngle = angles[i][1] - gapAngle / 2;

            if (i == current_mode)
            {
                setColor(31, 31, 0); // Active mode: Bright Yellow
                drawPie(centerX, centerY, radius, startAngle, endAngle);
            }
            else
            {
                setColor(10, 10, 10); // Inactive mode: Dark gray
                drawPie(centerX, centerY, radius - 2, startAngle, endAngle);
            }
            flushBuffer();
        }

        // Adjusted icon positions for landscape mode
        struct IconPosition
        {
            int x, y;
        };

        IconPosition positions[4] = {
            {25, 130}, // CAMERA
            {25, 130}, // SOUND
            {25, 130}, // CALL
            {25, 130}  // TORCH
        };

        // Draw correct icon for the current mode
        switch (current_mode)
        {
        case CAMERA:
            animateCameraIcon(positions[0].x, positions[0].y);
            break;
        case SOUND:
            drawImage(positions[1].x, positions[1].y, mic, 16, 16);
            break;
        case CALL:
            drawImage(positions[2].x, positions[2].y, mic, 16, 16);
            break;
        case TORCH:
            drawImage(positions[3].x, positions[3].y, cam_on_28_28, 28, 28);
            break;
        }

        flushBuffer(); // Update display
    }

    void animateCameraIcon(int x, int y)
    {
        // Transition sizes for smooth effect
        struct ImageSize
        {
            const uint16_t *image;
            int size;
        };

        ImageSize sizes[] = {
            {cam_on_24_24, 24},
            {cam_on_28_28, 28},
            {cam_on_32_32, 32}};

        int numSizes = sizeof(sizes) / sizeof(sizes[0]);

        for (int i = 0; i < numSizes; i++)
        {
            // Instead of clearImage, we overwrite with a background rectangle
            setColor(0, 0, 0); // Black background
            filledRect(x, y, sizes[i].size, sizes[i].size);

            // Draw the next image
            drawImage(x, y, sizes[i].image, sizes[i].size, sizes[i].size);
            flushBuffer();
         //   delay(50); // Small delay for smooth transition
        }
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

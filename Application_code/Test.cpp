#include <iostream>

extern "C"
{
#include "../st7735s.h"
#include "../fonts.h"
#include "../gfx.h"
#include "../image.h"
}

enum Mode
{
    CAMERA,
    SOUND,
    CALL,
    TORCH
};

struct ImageSize
{
    const uint16_t *image_24;
    const uint16_t *image_32;
};

int i = 0;
Mode current_mode = CAMERA;

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90);
        fillScreen();
        flushBuffer();
        while (true)
        {
            drawUI();
            waitForButtonPress();
        }
    }

    void drawUI()
    {
        setColor(2, 2, 2);
        fillScreen();

        drawImage(5, 12, battery_good, 24, 24);
        drawImage(55, 15, signal, 20, 18);

        struct IconPosition
        {
            int x, y;
        };

        IconPosition positions[4] = {
            {45, 60},  // CAMERA
            {10, 60},  // SOUND
            {45, 95}, // CALL
            {10, 95}  // TORCH
        };

        ImageSize imageSets[4] = {
            {cam_on_16_16, cam_on_32_32},
            {mic_16_16, mic_32_32},
            {cam_on_16_16, cam_on_32_32},
            {mic_16_16, mic_32_32}};

        for (int i = 0; i < 4; i++)
        {
            int iconSize = (i == current_mode) ? 32 : 24; // Determine the size
            int offset = (i == current_mode) ? -4 : 0;    // Center adjustment for bigger icons

            // **Highlight the selected icon background**
            if (i == current_mode)
            {
               // setColor(100, 100, 255); // Highlight color (light blue)
                //filledRect(positions[i].x - 4, positions[i].y - 4, 36, 36);
            }

            // **Draw icon centered at its position**
            drawImage(positions[i].x + offset, positions[i].y + offset, 
                      (i == current_mode) ? imageSets[i].image_32 : imageSets[i].image_24, 
                      iconSize, iconSize);
        }


        flushBuffer();
    }

    void waitForButtonPress()
    {
        while (!isButtonPressed())
        {
        }
        updateMode();
        _Delay(5000);
    }

    bool isButtonPressed()
    {
        static int counter = 0;
        counter++;
        return (counter % 1000000 == 0);
    }

    void updateMode()
    {
        if (i == 1)
        {
            current_mode = SOUND;
        }
        else if (i == 2)
        {
            current_mode = CALL;
        }
        else if (i == 3)
        {
            current_mode = TORCH;
        }
        else if (i == 4)
        {
            current_mode = CAMERA;
            i = 0;
        }
        i++;
    }
};

int main()
{
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

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
Mode next_mode = SOUND;

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
            {45, 95},  // CALL
            {10, 95}   // TORCH
        };

        ImageSize imageSets[4] = {
            {cam_on_24_24, cam_on_32_32},
            {mic_24_24, mic_32_32},
            {cam_on_24_24, sound_32_32},
            {mic_24_24, mic_32_32}};

        for (int i = 0; i < 4; i++)
        {
            bool isSelected = (i == current_mode || i == next_mode);
            int iconSize = isSelected ? 32 : 24;
            int offset = isSelected ? -4 : 0;

            drawImage(positions[i].x + offset, positions[i].y + offset, 
                      isSelected ? imageSets[i].image_32 : imageSets[i].image_24, 
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
        if (i == 0)
        {
            current_mode = CAMERA;
            next_mode = SOUND;
        }
        else if (i == 1)
        {
            current_mode = SOUND;
            next_mode = CALL;
        }
        else if (i == 2)
        {
            current_mode = CALL;
            next_mode = TORCH;
        }
        else if (i == 3)
        {
            current_mode = TORCH;
            next_mode = CAMERA;
            i = -1;
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

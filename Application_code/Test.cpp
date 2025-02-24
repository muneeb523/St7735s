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

Mode current_mode = CAMERA;

class DisplayExample
{
public:
    void run()
    {
        ST7735S_Init();
        setOrientation(R90);

        while (true)
        {
            drawUI();
            waitForButtonPress();
        }
    }

    void drawUI()
    {
        fillScreen();
        setColor(0, 0, 0);

        drawImage(5, 12, battery_good, 24, 24);
        drawImage(55, 15, signal, 20, 18);
        flushBuffer();

        struct IconPosition
        {
            int x, y;
        };

        IconPosition positions[4] = {
            {50, 60},  // CAMERA
            {25, 130}, // SOUND
            {80, 130}, // CALL
            {50, 180}  // TORCH
        };

        struct ImageSize
        {
            const uint16_t *image_16;
            const uint16_t *image_24;
            const uint16_t *image_28;
            const uint16_t *image_32;
        };

        ImageSize imageSets[4] = {
            {cam_on_16_16, cam_on_24_24, cam_on_28_28, cam_on_32_32},
            {mic_16_16, mic_24_24, mic_28_28, mic_32_32},
            {cam_on_16_16, cam_on_24_24, cam_on_28_28, cam_on_32_32},
            {mic_16_16, mic_24_24, mic_28_28, mic_32_32}};

        for (int i = 0; i < 4; i++)
        {
            if (i == current_mode)
                animateIcon(imageSets[i], positions[i].x, positions[i].y);
            else
                drawImage(positions[i].x, positions[i].y, imageSets[i].image_16, 16, 16);
        }
        flushBuffer();
    }

    void animateIcon(ImageSize &images, int x, int y)
    {
        const uint16_t *sizes[] = {images.image_16, images.image_24, images.image_28, images.image_32};
        int dimension[] = {16, 24, 28, 32};
        int numSizes = sizeof(dimension) / sizeof(dimension[0]);

        for (int i = 0; i < numSizes; i++)
        {
            drawImage(x, y, sizes[i], dimension[i], dimension[i]);
            flushBuffer();
            _Delay(5);
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
        return (counter % 1000000 == 0);
    }

    void updateMode()
    {
        current_mode = static_cast<Mode>((current_mode + 1) % 4);
    }
};

int main()
{
    std::cout << "Start" << std::endl;
    DisplayExample display;
    display.run();
    return 0;
}

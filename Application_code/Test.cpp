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
int i=0;
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
            {45, 60},  // CAMERA
            {10, 60},  // SOUND
            {45, 100}, // CALL
            {10, 100}  // TORCH
        };

        ImageSize imageSets[4] = {
            {cam_on_24_24, cam_on_32_32},
            {mic_24_24, mic_32_32},
            {cam_on_24_24, cam_on_32_32},
            {mic_24_24, mic_32_32}};

        for (int i = 0; i < 4; i++)
        {
            if (i == current_mode){
                drawImage(positions[i].x, positions[i].y, imageSets[i].image_32, 32, 32);
            }
            else{
                drawImage(positions[i].x, positions[i].y, imageSets[i].image_24, 24, 24);
            }
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
        if(i==1){
        current_mode =SOUND;
        }
        else if(i==2){
            current_mode =CALL;

        }
        else if(i==3){
            current_mode =TORCH;

        }
        else if(i==4){
            current_mode =CAMERA;
            i=0;
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

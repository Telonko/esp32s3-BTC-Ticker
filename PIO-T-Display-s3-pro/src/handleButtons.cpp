#include <Arduino.h>
#include "WiFiProvHelper.h"
#include "pin_config.h"
#include "BinanceWebSocket.h"
#include "handleButtons.h"

#define resetTime 5000  // 5 seconds for reset trigger
#define rotateTime 4500 // 5 seconds for rotate screen (including delay)

unsigned long button1_PressedTime = 0;
unsigned long button2_PressedTime = 0;
unsigned int button2_PressedCounter = 0;
bool button1_hold = false;
bool button2_hold = false;

// Check if reset button (GPIO0) is pressed and held for 5 seconds
void handleButton1()
{
    if (digitalRead(PIN_BUTTON_1) == LOW)
    {
        if (!button1_hold)
        {
            button1_hold = true;
            button1_PressedTime = millis();
        }
        else if (millis() - button1_PressedTime >= resetTime)
        {
            resetProvisioning(); // Reset Wi-Fi credentials if button held for 5 seconds
        }
    }
    else if (button1_hold)
    {
        button1_hold = false;
    }
}

void handleButton2()
{
    // Check if 2 button (GPIO21) is pressed or held for few seconds
    if (digitalRead(PIN_BUTTON_2) == LOW)
    {
        if (!button2_hold)
        {
            button2_PressedCounter++;
            button2_hold = true;
            button2_PressedTime = millis();

            // triggering from second push.
            if (button2_PressedCounter > 1)
            {
                if (button2_PressedCounter - 1 >= sizeof screenTickers / sizeof *screenTickers)
                {
                    button2_PressedCounter = 0;
                }

                if (button2_PressedCounter > 1)
                {
                    currentTicker = screenTickers[button2_PressedCounter - 1];
                }
                else
                {
                    currentTicker = screenTickers[0];
                }
                initBinanceWebSocket(); // switch between tickers
            }
        }
        else if (button2_PressedCounter == 1 && millis() - button2_PressedTime >= rotateTime)
        {
            Serial.println("[DEBUG] Toggle screen rotation");
            toggleScreenRotation(); //todo change me to something else cause of bugged pixels after rotation
            button2_PressedCounter = 0;
        }
    }
    else if (button2_hold)
    {
        button2_hold = false;
    }
    // refresh counter. let's switch tickers from the beggining.
    else if (button2_PressedCounter == 1 && millis() - button2_PressedTime > rotateTime * 2)
    {
        button2_PressedCounter = 0;
    }
}
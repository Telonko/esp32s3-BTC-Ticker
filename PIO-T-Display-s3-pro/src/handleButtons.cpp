#include <Arduino.h>
#include "WiFiProvHelper.h"
#include "pin_config.h"
#include "BinanceWebSocket.h"
#include "Alerts.h"
#include "handleButtons.h"

#define resetTime 5000  // hold button 1 to reset Wi-Fi credentials
#define rotateTime 4500 // hold button 2 to rotate the screen
#define debounceTime 30 // shorter presses are contact bounce

unsigned long button1_PressedTime = 0;
unsigned long button2_PressedTime = 0;
bool button1_hold = false;
bool button2_hold = false;
bool button2_longDone = false;

// Button 1 (GPIO0): press dismisses an alert, hold 5 s resets Wi-Fi credentials
void handleButton1()
{
    if (digitalRead(PIN_BUTTON_1) == LOW)
    {
        if (!button1_hold)
        {
            button1_hold = true;
            button1_PressedTime = millis();
            alertDismiss();
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

// Button 2 (GPIO21): short press shows the next pair (or dismisses an alert),
// hold rotates the screen
void handleButton2()
{
    bool pressed = digitalRead(PIN_BUTTON_2) == LOW;

    if (pressed && !button2_hold)
    {
        button2_hold = true;
        button2_longDone = false;
        button2_PressedTime = millis();
    }
    else if (pressed && !button2_longDone && millis() - button2_PressedTime >= rotateTime)
    {
        Serial.println("[DEBUG] Toggle screen rotation");
        toggleScreenRotation();
        button2_longDone = true;
    }
    else if (!pressed && button2_hold)
    {
        button2_hold = false;
        if (!button2_longDone && millis() - button2_PressedTime >= debounceTime)
        {
            if (alertActive())
                alertDismiss();
            else
                selectNextTicker();
        }
    }
}

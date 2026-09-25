#include <Arduino.h>
#include "WiFiProvHelper.h"
#include "pin_config.h"
#include "BinanceWebSocket.h"
#include "Alerts.h"
#include "ChartView.h"
#include "handleButtons.h"

#define setupTime 5000  // hold button 1: setup access point + page password reset
#define rotateTime 4500 // hold button 2 to rotate the screen
#define debounceTime 30 // shorter presses are contact bounce
#define doubleClickTime 400 // second click within this time = double click
#define ipShowTime 10000

unsigned long button1_PressedTime = 0;
unsigned long button2_PressedTime = 0;
bool button1_hold = false;
bool button2_hold = false;
bool button2_longDone = false;
bool button1_longDone = false;
unsigned long button1_ReleasedTime = 0;
bool button1_clickPending = false;

// Button 1 (GPIO0): click switches the bottom row high/low <-> candles
// (or dismisses an alert),
// double click shows the IP address, hold 5 s starts the setup access point
// and resets the page password; click on the setup screen closes it
void handleButton1()
{
    // A single click waits to see whether a second one follows
    if (button1_clickPending && millis() - button1_ReleasedTime >= doubleClickTime)
    {
        button1_clickPending = false;
        if (wifiShowSetupScreen())
            wifiCloseSetup();
        else if (alertActive())
            alertDismiss();
        else
            chartViewToggle();
    }

    if (digitalRead(PIN_BUTTON_1) == LOW)
    {
        if (!button1_hold)
        {
            button1_hold = true;
            button1_longDone = false;
            button1_PressedTime = millis();
        }
        else if (!button1_longDone && millis() - button1_PressedTime >= setupTime)
        {
            button1_longDone = true;
            wifiSetupMode();
        }
    }
    else if (button1_hold)
    {
        button1_hold = false;
        if (!button1_longDone && millis() - button1_PressedTime >= debounceTime)
        {
            if (button1_clickPending)
            {
                button1_clickPending = false;
                showIpAddress(ipShowTime);
            }
            else
            {
                button1_clickPending = true;
                button1_ReleasedTime = millis();
            }
        }
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

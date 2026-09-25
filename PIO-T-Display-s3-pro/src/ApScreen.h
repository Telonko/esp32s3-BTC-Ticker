#ifndef AP_SCREEN_H
#define AP_SCREEN_H

#include <lvgl.h>

// Setup screen shown while the board runs its own access point:
// QR code to join it plus SSID, password and the settings page address.

void apScreenInit();
void apScreenShow(const char *ssid, const char *pass, const char *ip);
lv_obj_t *apScreen();

#endif // AP_SCREEN_H

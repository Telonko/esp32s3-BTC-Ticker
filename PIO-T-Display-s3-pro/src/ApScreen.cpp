#include "ApScreen.h"
#include <Arduino.h>
#include <ui.h>

#define QR_SIZE 180
#define TEXT_COLOR 0xFFCA41 // same yellow as the ticker screen

static lv_obj_t *screen = nullptr;
static lv_obj_t *qr = nullptr;
static lv_obj_t *text = nullptr;

void apScreenInit()
{
    screen = lv_obj_create(NULL);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);

    // White frame = quiet zone around the QR code, phones need it
    lv_obj_t *frame = lv_obj_create(screen);
    lv_obj_set_size(frame, QR_SIZE + 16, QR_SIZE + 16);
    lv_obj_set_pos(frame, 16, (240 - QR_SIZE - 16) / 2);
    lv_obj_set_style_bg_color(frame, lv_color_white(), 0);
    lv_obj_set_style_border_width(frame, 0, 0);
    lv_obj_set_style_radius(frame, 4, 0);
    lv_obj_set_style_pad_all(frame, 8, 0);
    lv_obj_clear_flag(frame, LV_OBJ_FLAG_SCROLLABLE);

    qr = lv_qrcode_create(frame, QR_SIZE, lv_color_black(), lv_color_white());
    lv_obj_center(qr);

    // Font25 is monospaced, 15 px per char: 20 chars fit in 300 px
    text = lv_label_create(screen);
    lv_obj_set_pos(text, 228, 30);
    lv_obj_set_width(text, 300);
    lv_obj_set_style_text_font(text, &ui_font_Font25, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(TEXT_COLOR), 0);
    lv_obj_set_style_text_line_space(text, 6, 0);
    lv_label_set_text(text, "");
}

void apScreenShow(const char *ssid, const char *pass, const char *ip)
{
    // Standard Wi-Fi QR: phone cameras offer to join the network
    char payload[96];
    snprintf(payload, sizeof(payload), "WIFI:T:WPA;S:%s;P:%s;;", ssid, pass);
    lv_qrcode_update(qr, payload, strlen(payload));

    // ASCII only: the fonts have no Cyrillic
    lv_label_set_text_fmt(text, "Wi-Fi setup\n\n%s\npass: %s\nhttp://%s", ssid, pass, ip);
}

lv_obj_t *apScreen()
{
    return screen;
}

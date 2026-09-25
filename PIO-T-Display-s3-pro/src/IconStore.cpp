#include <Arduino.h>
#include <LittleFS.h>
#include <sys/stat.h>
#include <lvgl.h>
#include <ui.h>
#include "IconStore.h"

#define ICON_DIR "/icons"
#define ICON_BOX 26 // px on screen, like the built-in icons

static bool mounted = false;
static lv_obj_t *iconImg = nullptr;

// Decoded once when shown: with LV_IMG_CACHE_DEF_SIZE 0 LVGL would decode
// the PNG again on every redraw
static lv_img_dsc_t decoded;
static uint8_t *decodedData = nullptr;

bool iconStoreBegin()
{
    mounted = LittleFS.begin(true);
    if (!mounted)
        Serial.println("[ICON] LittleFS mount failed");
    else if (!LittleFS.exists(ICON_DIR))
        LittleFS.mkdir(ICON_DIR);
    return mounted;
}

String iconPath(const char *name)
{
    return String(ICON_DIR "/") + name + ".png";
}

bool iconExists(const char *name)
{
    // LittleFS.exists() logs an error for every missing file
    struct stat st;
    return mounted && stat(("/littlefs" + iconPath(name)).c_str(), &st) == 0;
}

static uint32_t readBE32(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

const char *iconValidate(const uint8_t *data, size_t size)
{
    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (size < 24 || memcmp(data, signature, 8) != 0 || memcmp(data + 12, "IHDR", 4) != 0)
        return "Это не PNG";
    uint32_t w = readBE32(data + 16), h = readBE32(data + 20);
    if (w == 0 || h == 0 || w > ICON_MAX_SIZE || h > ICON_MAX_SIZE)
        return "Картинка больше 64×64 px";
    return nullptr;
}

void iconDelete(const char *name)
{
    if (mounted)
        LittleFS.remove(iconPath(name));
}

void iconViewInit()
{
    // Same spot as the built-in coin icons
    iconImg = lv_img_create(ui_ticker);
    lv_obj_set_align(iconImg, LV_ALIGN_CENTER);
    lv_obj_set_pos(iconImg, 134, -94);
    lv_obj_add_flag(iconImg, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(iconImg, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
}

void iconViewHide()
{
    if (iconImg)
        lv_obj_add_flag(iconImg, LV_OBJ_FLAG_HIDDEN);
}

bool iconViewShow(const char *name)
{
    if (!iconImg || !iconExists(name))
    {
        iconViewHide();
        return false;
    }

    File file = LittleFS.open(iconPath(name), "r");
    size_t size = file.size();
    if (size == 0 || size > ICON_MAX_BYTES)
    {
        file.close();
        iconViewHide();
        return false;
    }
    uint8_t *png = (uint8_t *)ps_malloc(size);
    file.read(png, size);
    file.close();

    // Raw PNG as an LVGL image source, decoded by the built-in PNG decoder
    lv_img_dsc_t raw = {};
    raw.header.cf = LV_IMG_CF_RAW_ALPHA;
    raw.data_size = size;
    raw.data = png;

    lv_img_decoder_dsc_t dsc;
    bool ok = lv_img_decoder_open(&dsc, &raw, lv_color_black(), 0) == LV_RES_OK && dsc.img_data;
    if (ok)
    {
        uint32_t w = dsc.header.w, h = dsc.header.h;
        size_t bytes = w * h * LV_IMG_PX_SIZE_ALPHA_BYTE;

        lv_img_set_src(iconImg, NULL); // release the previous buffer first
        free(decodedData);
        decodedData = (uint8_t *)ps_malloc(bytes);
        memcpy(decodedData, dsc.img_data, bytes);
        lv_img_decoder_close(&dsc);

        decoded = {};
        decoded.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        decoded.header.w = w;
        decoded.header.h = h;
        decoded.data_size = bytes;
        decoded.data = decodedData;

        lv_img_set_src(iconImg, &decoded);
        uint32_t side = max(w, h);
        lv_img_set_zoom(iconImg, side > ICON_BOX ? LV_IMG_ZOOM_NONE * ICON_BOX / side : LV_IMG_ZOOM_NONE);
        lv_obj_clear_flag(iconImg, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        Serial.printf("[ICON] Failed to decode %s\n", iconPath(name).c_str());
        iconViewHide();
    }
    free(png);
    return ok;
}

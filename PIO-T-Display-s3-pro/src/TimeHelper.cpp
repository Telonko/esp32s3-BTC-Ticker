#include "TimeHelper.h"
#include "Config.h"  // Include the configuration header
#include <Arduino.h>
#include <time.h>
#include <lvgl.h>
#include "ui.h"
#include <Preferences.h>

#define PREFS_NAMESPACE "ticker"
#define PREFS_TZ_KEY "tz"

// Any time before this means SNTP has not synced yet
#define MIN_VALID_EPOCH 1700000000

static bool ntpSyncStarted = false;
static int shownMinute = -2; // -1 means "--:--" is shown
static char timeZone[48] = "";

// POSIX TZ: the sign is inverted, "<+04>-4" is UTC+4
const TimeZoneOption timeZones[] = {
    {"UTC", "UTC0"},
    {"Лондон (UTC+0/+1)", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Берлин, Париж, Варшава (UTC+1/+2)", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"Калининград (UTC+2)", "EET-2"},
    {"Киев, Рига, Афины (UTC+2/+3)", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
    {"Москва, Минск (UTC+3)", "MSK-3"},
    {"Стамбул (UTC+3)", "<+03>-3"},
    {"Самара, Дубай, Тбилиси, Ереван, Баку (UTC+4)", "<+04>-4"},
    {"Екатеринбург, Ташкент, Алматы (UTC+5)", "<+05>-5"},
    {"Индия (UTC+5:30)", "IST-5:30"},
    {"Омск, Бишкек (UTC+6)", "<+06>-6"},
    {"Новосибирск, Красноярск, Бангкок (UTC+7)", "<+07>-7"},
    {"Иркутск, Сингапур, Пекин (UTC+8)", "<+08>-8"},
    {"Якутск, Токио, Сеул (UTC+9)", "<+09>-9"},
    {"Владивосток (UTC+10)", "<+10>-10"},
    {"Сидней (UTC+10/+11)", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Магадан (UTC+11)", "<+11>-11"},
    {"Камчатка (UTC+12)", "<+12>-12"},
    {"Нью-Йорк (UTC-5/-4)", "EST5EDT,M3.2.0,M11.1.0"},
    {"Чикаго (UTC-6/-5)", "CST6CDT,M3.2.0,M11.1.0"},
    {"Денвер (UTC-7/-6)", "MST7MDT,M3.2.0,M11.1.0"},
    {"Лос-Анджелес (UTC-8/-7)", "PST8PDT,M3.2.0,M11.1.0"},
};
const int timeZoneCount = sizeof(timeZones) / sizeof(timeZones[0]);

const char *timeZoneGet() {
    if (!timeZone[0]) {
        strlcpy(timeZone, TIME_ZONE, sizeof(timeZone));
        Preferences prefs;
        prefs.begin(PREFS_NAMESPACE, false);
        if (prefs.isKey(PREFS_TZ_KEY))
            prefs.getString(PREFS_TZ_KEY, timeZone, sizeof(timeZone));
        prefs.end();
    }
    return timeZone;
}

void timeZoneSet(const char *posix) {
    strlcpy(timeZone, posix, sizeof(timeZone));
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(PREFS_TZ_KEY, timeZone);
    prefs.end();

    setenv("TZ", timeZone, 1);
    tzset();
    shownMinute = -2; // redraw the clock right away
    Serial.printf("[TIME] Time zone %s\n", timeZone);
}

// Starts SNTP in the background and returns immediately.
// The SNTP client keeps retrying and re-syncing on its own.
void initiateNTPTimeSync() {
    if (ntpSyncStarted) {
        return;
    }
    ntpSyncStarted = true;
    configTzTime(timeZoneGet(), "pool.ntp.org", "time.nist.gov");
    Serial.println("Started NTP time sync...");
}

bool isTimeSynchronized() {
    return time(nullptr) > MIN_VALID_EPOCH;
}

// Updates the time label, redrawing it only when the minute changes
void updateTimeAndDate() {
    if (!isTimeSynchronized()) {
        if (shownMinute != -1) {
            lv_label_set_text(ui_Label_time, "--:--");
            shownMinute = -1;
        }
        return;
    }

    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);

    int minute = timeInfo.tm_hour * 60 + timeInfo.tm_min;
    if (minute == shownMinute) {
        return;
    }
    shownMinute = minute;

    char timeStr[6];
    strftime(timeStr, sizeof(timeStr), "%H:%M", &timeInfo);
    lv_label_set_text(ui_Label_time, timeStr);
}

int localHour() {
    if (!isTimeSynchronized()) {
        return -1;
    }
    time_t now = time(nullptr);
    struct tm timeInfo;
    localtime_r(&now, &timeInfo);
    return timeInfo.tm_hour;
}

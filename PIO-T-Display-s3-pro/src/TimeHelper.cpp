#include "TimeHelper.h"
#include "Config.h"  // Include the configuration header
#include <Arduino.h>
#include <time.h>
#include <lvgl.h>
#include "ui.h"

// Any time before this means SNTP has not synced yet
#define MIN_VALID_EPOCH 1700000000

static bool ntpSyncStarted = false;

// Starts SNTP in the background and returns immediately.
// The SNTP client keeps retrying and re-syncing on its own.
void initiateNTPTimeSync() {
    if (ntpSyncStarted) {
        return;
    }
    ntpSyncStarted = true;
    configTzTime(TIME_ZONE, "pool.ntp.org", "time.nist.gov");
    Serial.println("Started NTP time sync...");
}

bool isTimeSynchronized() {
    return time(nullptr) > MIN_VALID_EPOCH;
}

// Updates the time label, redrawing it only when the minute changes
void updateTimeAndDate() {
    static int shownMinute = -2; // -1 means "--:--" is shown

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

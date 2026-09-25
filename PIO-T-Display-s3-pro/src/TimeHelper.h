#ifndef TIME_HELPER_H
#define TIME_HELPER_H

// Function declarations for time syncing and updating the UI
void initiateNTPTimeSync();  // Initiates non-blocking NTP sync
bool isTimeSynchronized();   // Returns true if NTP time is synced
void updateTimeAndDate();    // Updates the UI with current time and date
int localHour();             // 0..23, or -1 until NTP time is synced

// Time zone as a POSIX TZ string, stored in NVS (default: TIME_ZONE)
struct TimeZoneOption {
    const char *label;
    const char *posix;
};
extern const TimeZoneOption timeZones[];
extern const int timeZoneCount;

const char *timeZoneGet();
void timeZoneSet(const char *posix); // applies at once

#endif // TIME_HELPER_H

#include "OtaGuard.h"
#include <Arduino.h>
#include <Preferences.h>
#include <esp_ota_ops.h>

#define PREFS_NAMESPACE "ota"
#define KEY_PENDING "pending"
#define KEY_PREVIOUS "prev"
#define KEY_BOOTS "boots"

static bool pending = false;

void otaGuardBoot()
{
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    pending = prefs.getBool(KEY_PENDING, false);
    if (!pending)
    {
        prefs.end();
        return;
    }

    uint8_t boots = prefs.getUChar(KEY_BOOTS, 0) + 1;
    prefs.putUChar(KEY_BOOTS, boots);
    String previous = prefs.getString(KEY_PREVIOUS, "");
    Serial.printf("[OTA] Unconfirmed firmware, boot %u of %u\n", boots, OTA_GUARD_MAX_BOOTS);

    if (boots > OTA_GUARD_MAX_BOOTS)
    {
        prefs.putBool(KEY_PENDING, false);
        prefs.end();
        pending = false;

        const esp_partition_t *partition = previous.length()
            ? esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_ANY, previous.c_str())
            : nullptr;
        if (partition && esp_ota_set_boot_partition(partition) == ESP_OK)
        {
            Serial.printf("[OTA] New firmware keeps restarting, rolling back to %s\n", previous.c_str());
            delay(100);
            ESP.restart();
        }
        Serial.println("[OTA] Rollback impossible, keeping this firmware");
        return;
    }
    prefs.end();
}

void otaGuardArm()
{
    const esp_partition_t *running = esp_ota_get_running_partition();

    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putString(KEY_PREVIOUS, running ? running->label : "");
    prefs.putUChar(KEY_BOOTS, 0);
    prefs.putBool(KEY_PENDING, true);
    prefs.end();
    Serial.printf("[OTA] Previous firmware in %s kept for rollback\n", running ? running->label : "?");
}

void otaGuardLoop()
{
    if (!pending || millis() < OTA_GUARD_CONFIRM_MS)
        return;

    pending = false;
    Preferences prefs;
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(KEY_PENDING, false);
    prefs.end();
    Serial.println("[OTA] New firmware confirmed");
}

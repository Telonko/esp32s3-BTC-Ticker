#ifndef OTA_GUARD_H
#define OTA_GUARD_H

// Rollback for firmware updated over Wi-Fi. The prebuilt Arduino SDK has no
// app rollback, so: after an OTA the new image is "unconfirmed"; if it
// restarts more than OTA_GUARD_MAX_BOOTS times before running
// OTA_GUARD_CONFIRM_MS, the board boots the previous image again.

#define OTA_GUARD_MAX_BOOTS 3
#define OTA_GUARD_CONFIRM_MS 60000

// Call first thing in setup(): counts boots of an unconfirmed image and
// rolls back (restarts) when there were too many
void otaGuardBoot();

// Call right before restarting into a freshly written image
void otaGuardArm();

// Call from loop(): confirms the image after OTA_GUARD_CONFIRM_MS uptime
void otaGuardLoop();

#endif // OTA_GUARD_H

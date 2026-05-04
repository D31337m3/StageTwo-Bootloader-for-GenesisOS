#ifndef GENESIS_RECOVERY_H
#define GENESIS_RECOVERY_H

#include "esp_err.h"
#include <stdbool.h>

/**
 * Initialize recovery system.
 * Checks NVS for the recovery flag.
 */
esp_err_t genesis_recovery_init(void);

/**
 * Check if the system should enter recovery mode.
 * Returns true if the flag is set or if forced by hardware.
 */
bool genesis_recovery_needed(void);

/**
 * Start the recovery/repair utility.
 * This will mount the filesystem, look for /system/recovery.bin,
 * and restore the system partition.
 */
esp_err_t genesis_recovery_start(void);

/**
 * Clear the recovery flag in NVS.
 */
void genesis_recovery_clear_flag(void);

/**
 * Set the recovery flag in NVS.
 */
void genesis_recovery_set_flag(void);

/**
 * Apply a downloaded update from /system/recovery.bin.
 * If major is true, it performs a full partition re-flash.
 */
esp_err_t genesis_recovery_apply_update(bool major);

/**
 * Check if there is a pending update waiting to be installed.
 */
bool genesis_recovery_has_pending_update(void);

/**
 * Set the pending update flag.
 */
void genesis_recovery_set_pending_update(bool pending);

#endif // GENESIS_RECOVERY_H

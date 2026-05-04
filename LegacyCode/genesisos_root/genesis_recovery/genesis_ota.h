/*
 * genesis_ota.h
 *
 * Automated Update & Recovery Manager for Genesis OS
 */

#ifndef GENESIS_OTA_H
#define GENESIS_OTA_H

#include <stdbool.h>
#include "esp_err.h"

// OTA States
typedef enum {
    OTA_IDLE,
    OTA_CHECKING,
    OTA_DOWNLOADING,
    OTA_EXTRACTING,
    OTA_FLASHING_FACTORY,
    OTA_COMPLETE,
    OTA_ERROR
} genesis_ota_state_t;

// Initialization
void genesis_ota_init(void);

// Periodic task (called from background loop)
void genesis_ota_update_tick(void);

// Manual trigger
esp_err_t genesis_ota_start_check(void);

// Factory Flash Logic
esp_err_t genesis_factory_flash_from_file(const char* path);

// Status
genesis_ota_state_t genesis_ota_get_state(void);
int genesis_ota_get_progress(void);

#endif // GENESIS_OTA_H

#pragma once

#include "esp_err.h"

namespace stage2_backup {

// Writes a backup archive of the entire `/user` filesystem to the SD card.
// `archive_path` should be under `/sd` (ex: "/sd/stage2/user_backup.gosar").
esp_err_t backup_user_to_sd(const char* archive_path);

// Restores `/user` from a backup archive on the SD card.
// This is destructive: it wipes `/user` before restoring.
esp_err_t restore_user_from_sd(const char* archive_path);

}


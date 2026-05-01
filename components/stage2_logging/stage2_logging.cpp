#include "stage2_logging.hpp"
#include "esp_log.h"

static const char* TAG = "StageTwo";

namespace stage2_logging {

void init() {}

void info(const char* message)  { ESP_LOGI(TAG, "%s", message); }
void warn(const char* message)  { ESP_LOGW(TAG, "%s", message); }
void error(const char* message) { ESP_LOGE(TAG, "%s", message); }
void fatal(const char* message) { ESP_LOGE(TAG, "FATAL: %s", message); }

}

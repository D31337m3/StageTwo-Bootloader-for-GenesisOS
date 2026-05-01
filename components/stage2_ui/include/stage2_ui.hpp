#pragma once
#include "lvgl.h"

namespace stage2_ui {

enum class SplashResult {
    BootGenesisOS,
    OpenBootMenu,
    OpenRecovery
};

void init();
void tick();

SplashResult show_splash_and_detect_boot_request();
void show_boot_menu();
void show_advanced_menu();
void show_progress(const char* title, int percent);
void show_error(const char* message);

bool confirm_destructive_action(
    const char* title,
    const char* desc,
    const char* details
);

// Internal UI functions
void create_splash_screen();
void create_boot_menu();
void create_advanced_menu();
void create_confirmation_dialog(
    const char* title,
    const char* desc,
    const char* details,
    bool* confirmed
);
void destroy_current_screen();

}

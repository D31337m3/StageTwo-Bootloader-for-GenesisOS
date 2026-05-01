#pragma once
#include <stdint.h>

namespace stage2_input {

enum class ButtonEvent {
    None,
    ShortPress,
    LongPress,
    DoublePress,
    Hold5s,
    HoldActive,
    Released
};

void init();
ButtonEvent poll_button();
bool is_boot_button_held();
uint32_t held_ms();

}

#pragma once
#include <stdint.h>

namespace genesis_theme {

struct Color {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

void init();

Color primary();
Color accent();
Color purple_os();
Color bg_main();
Color text_primary();
Color success();
Color warning();
Color error();

}

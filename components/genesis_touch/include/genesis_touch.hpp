#pragma once
#include "lvgl.h"

namespace genesis_touch {

struct Point {
    int x;
    int y;
    bool pressed;
};

bool init();
Point read();
lv_indev_t* get_indev();

}

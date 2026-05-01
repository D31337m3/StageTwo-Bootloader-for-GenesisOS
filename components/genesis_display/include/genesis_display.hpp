#pragma once
#include "lvgl.h"

namespace genesis_display {
    bool init();
    int width();
    int height();
    lv_disp_t* get_disp();
    void flush();
}

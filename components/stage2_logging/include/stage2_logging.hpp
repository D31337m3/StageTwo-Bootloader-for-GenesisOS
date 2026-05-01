#pragma once

namespace stage2_logging {
    void init();
    void info(const char* message);
    void warn(const char* message);
    void error(const char* message);
    void fatal(const char* message);
}

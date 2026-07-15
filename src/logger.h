#pragma once

#include <cstdio>

#define CONSOLE_COLOR_RESET   "\x1b[0m"
#define CONSOLE_COLOR_RED     "\x1b[31m"
#define CONSOLE_COLOR_GREEN   "\x1b[32m"
#define CONSOLE_COLOR_YELLOW  "\x1b[33m"
#define CONSOLE_COLOR_BLUE    "\x1b[34m"
#define CONSOLE_COLOR_MAGENTA "\x1b[35m"
#define CONSOLE_COLOR_CYAN    "\x1b[36m"

#define log(log_color, log_level, format, ...)                      \
    do                                                              \
    {                                                               \
        char buffer[256];                                           \
        std::snprintf(buffer, sizeof(buffer), format, __VA_ARGS__); \
        std::string_view file_name{__FILE__};                       \
        auto pos = file_name.find_last_of("\\/");                   \
        if (pos != std::string_view::npos)                          \
        {                                                           \
            file_name = file_name.substr(pos + 1);                  \
        }                                                           \
        std::printf(" %.*s:%d\t%s%s%s\t%s\n",                       \
                    static_cast<int>(file_name.size()),             \
                    file_name.data(),                               \
                    __LINE__,                                       \
                    log_color,                                      \
                    log_level,                                      \
                    CONSOLE_COLOR_RESET,                            \
                    buffer);                                        \
    } while (0)

#define log_info(...)  log(CONSOLE_COLOR_CYAN, "INFO", __VA_ARGS__)
#define log_warn(...)  log(CONSOLE_COLOR_YELLOW, "WARN", __VA_ARGS__)
#define log_error(...) log(CONSOLE_COLOR_RED, "ERROR", __VA_ARGS__)

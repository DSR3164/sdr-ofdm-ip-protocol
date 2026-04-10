#include <spdlog/fmt/bundled/color.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace logs
{
    extern spdlog::logger &sdr;
    extern spdlog::logger &tun;
    extern spdlog::logger &gui;
    extern spdlog::logger &dsp;
    extern spdlog::logger &main;
    extern spdlog::logger &socket;
} // namespace logs

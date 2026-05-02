#include <logger.hpp>

namespace logs
{
    inline spdlog::logger &create_logger(const std::string &name, const std::string &pattern)
    {
        auto &logger = *spdlog::stdout_color_mt(name);
        logger.set_pattern(pattern);
        return logger;
    }

    spdlog::logger &sdr = create_logger("SDR", "[%Y-%m-%d %H:%M:%S.%e] [\033[36m%n\033[0m]  [%^%l%$] %v");
    spdlog::logger &tun = create_logger("TUN", "[%Y-%m-%d %H:%M:%S.%e] [\033[32m%n\033[0m]  [%^%l%$] %v");
    spdlog::logger &gui = create_logger("GUI", "[%Y-%m-%d %H:%M:%S.%e] [\033[35m%n\033[0m]  [%^%l%$] %v");
    spdlog::logger &dsp = create_logger("DSP", "[%Y-%m-%d %H:%M:%S.%e] [\033[33m%n\033[0m]  [%^%l%$] %v");
    spdlog::logger &main = create_logger("MAIN", "[%Y-%m-%d %H:%M:%S.%e] [\033[31m%n\033[0m] [%^%l%$] %v");
    spdlog::logger &socket = create_logger("SOCK", "[%Y-%m-%d %H:%M:%S.%e] [\033[34m%n\033[0m] [%^%l%$] %v");
} // namespace logs

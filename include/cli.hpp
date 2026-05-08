#pragma once

#include "common.hpp"

#include <cxxopts.hpp>
#include <optional>
#include <spdlog/spdlog.h>

enum class Node { A, B };

struct LogConfig
{
    spdlog::level::level_enum sdr = spdlog::level::info;
    spdlog::level::level_enum tun = spdlog::level::info;
    spdlog::level::level_enum gui = spdlog::level::info;
    spdlog::level::level_enum dsp = spdlog::level::info;
    spdlog::level::level_enum main = spdlog::level::info;
    spdlog::level::level_enum socket = spdlog::level::info;
    void set_all(spdlog::level::level_enum level)
    {
        sdr = level;
        tun = level;
        gui = level;
        dsp = level;
        main = level;
        socket = level;
    }
};

struct CliConfig
{
    Modulation modulation = Modulation::QAM64;
    std::optional<Node> node;
    std::optional<double> rx_freq;
    std::optional<double> tx_freq;
    std::optional<std::string> ip; 
    LogConfig log;
};

std::optional<CliConfig> parse_cli(int argc, char *argv[]);

void set_cli_opts(SharedData &data, CliConfig &cfg);

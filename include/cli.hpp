#pragma once

#include "common.hpp"

#include <cxxopts.hpp>
#include <optional>

enum class Node { A, B };

struct CliConfig
{
    Modulation modulation = Modulation::QAM64;
    std::optional<Node> node;
    std::optional<double> rx_freq;
    std::optional<double> tx_freq;
};

std::optional<CliConfig> parse_cli(int argc, char *argv[]);

void set_cli_opts(SharedData &data, CliConfig &cfg);

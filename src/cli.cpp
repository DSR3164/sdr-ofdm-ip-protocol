#include "cli.hpp"
#include "logger.hpp"

#include <iostream>

static Modulation stringToModulation(const std::string &str)
{
    if (str == "BPSK")
        return Modulation::BPSK;
    if (str == "QPSK")
        return Modulation::QPSK;
    if (str == "QAM16")
        return Modulation::QAM16;
    if (str == "QAM64")
        return Modulation::QAM64;
    throw std::invalid_argument("Unknown modulation: " + str);
}

std::optional<CliConfig> parse_cli(int argc, char *argv[])
{
    cxxopts::Options options("sdr_app", "Software Defined Radio application");

    // clang-format off
    options.add_options()
    ("m,modulation", "Modulation scheme (BPSK, QPSK, QAM16, QAM64)", cxxopts::value<std::string>()->default_value("QAM64"))
    ("n,node", "Base Node settings (A / B)", cxxopts::value<std::string>())
    ("r,rx", "Set RX frequency (Hz)", cxxopts::value<double>()->implicit_value("2203000000"))
    ("t,tx", "Set TX frequency (Hz)", cxxopts::value<double>()->implicit_value("2203000000"))
    ("h,help", "Print usage");
    // clang-format on

    cxxopts::ParseResult result;
    try
    {
        result = options.parse(argc, argv);
    }
    catch (const cxxopts::exceptions::exception &e)
    {
        std::cerr << "Argument error: " << e.what() << std::endl;
        std::cerr << options.help() << std::endl;
        return std::nullopt;
    }

    if (result.count("help"))
    {
        std::cout << options.help() << std::endl;
        return std::nullopt;
    }

    CliConfig cfg;

    try
    {
        cfg.modulation = stringToModulation(result["modulation"].as<std::string>());
    }
    catch (const std::invalid_argument &e)
    {
        std::cerr << e.what() << std::endl;
        std::cerr << options.help() << std::endl;
        return std::nullopt;
    }

    if (result.count("n") && (result.count("rx") || result.count("tx")))
    {
        std::cerr << "Cannot use --node together with --rx/--tx" << std::endl;
        return std::nullopt;
    }

    if (result.count("n"))
    {
        const auto &node = result["n"].as<std::string>();
        if (node == "A" || node == "a")
            cfg.node = Node::A;
        else if (node == "B" || node == "b")
            cfg.node = Node::B;
        else
        {
            std::cerr << "Unknown node: '" << node << "', expected A or B" << std::endl;
            return std::nullopt;
        }
    }
    else if (result.count("rx"))
        cfg.rx_freq = result["rx"].as<double>();
    if (result.count("tx") && !result.count("n"))
        cfg.tx_freq = result["tx"].as<double>();

    return cfg;
}

void set_cli_opts(SharedData &data, CliConfig &cfg)
{
    data.dsp.ofdm_cfg.mod = cfg.modulation;

    if (cfg.rx_freq)
        data.sdr.set_rx_freq(*cfg.rx_freq);
    else if (cfg.tx_freq)
        data.sdr.set_tx_freq(*cfg.tx_freq);
    else if (cfg.node == Node::A)
        data.sdr.set_tx_freq(2230e6);
    else if (cfg.node == Node::B)
        data.sdr.set_rx_freq(2230e6);
}

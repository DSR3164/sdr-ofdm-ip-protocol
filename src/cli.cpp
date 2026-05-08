#include "cli.hpp"
#include "logger.hpp"
#include "config.hpp"

#include <iostream>
#include <string>

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

static spdlog::level::level_enum parseLevel(const std::string &name, const std::string &val)
{
    auto level = spdlog::level::from_str(val);
    if (level == spdlog::level::off && val != "off")
        throw std::invalid_argument("Unknown log level for " + name + ": '" + val + "'");
    return level;
}

std::optional<CliConfig> parse_cli(int argc, char *argv[])
{
    cxxopts::Options options("soip", "Software Defined Radio application");

    // clang-format off
    options.add_options()
    ("c,config", "Path to config file", cxxopts::value<std::string>()->default_value("../config/sdr.conf"))
    ("m,modulation", "Modulation scheme (BPSK, QPSK, QAM16, QAM64)", cxxopts::value<std::string>()->default_value("QAM64"))
    ("n,node", "Base Node settings (A / B)", cxxopts::value<std::string>())
    ("r,rx", "Set RX frequency (Hz)", cxxopts::value<double>()->implicit_value("2200000000"))
    ("t,tx", "Set TX frequency (Hz)", cxxopts::value<double>()->implicit_value("2230000000"))
    ("i,ip", "Set IP Adress", cxxopts::value<std::string>()->default_value("10.0.0.2"))
    ("log-level", "Log level for all (trace/debug/info/warn/error/critical)", cxxopts::value<std::string>()->default_value("info"))
    ("log-sdr", "Log level for SDR", cxxopts::value<std::string>())
    ("log-tun", "Log level for TUN", cxxopts::value<std::string>())
    ("log-gui", "Log level for GUI", cxxopts::value<std::string>())
    ("log-dsp", "Log level for DSP", cxxopts::value<std::string>())
    ("log-main", "Log level for MAIN", cxxopts::value<std::string>())
    ("log-socket", "Log level for SOCKET", cxxopts::value<std::string>())
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

    const std::string config_path = result["config"].as<std::string>();
    const bool config_explicit = result.count("config") > 0;

    if (config_explicit || std::filesystem::exists(config_path))
    {
        auto file_cfg = load_config(config_path);
        auto file_args = config_to_args(file_cfg);
        std::vector<const char *> merged_argv;
        merged_argv.push_back(argv[0]);
        for (const auto &a : file_args)
            merged_argv.push_back(a.c_str());
        for (int i = 1; i < argc; ++i)
            merged_argv.push_back(argv[i]);

        int merged_argc = static_cast<int>(merged_argv.size());

        try
        {
            result = options.parse(merged_argc, const_cast<char **>(merged_argv.data()));
        }
        catch (const cxxopts::exceptions::exception &e)
        {
            logs::main.critical("Argument error: {}", e.what());
            return std::nullopt;
        }
    }

    CliConfig cfg;

    try
    {
        const auto global = result["log-level"].as<std::string>();
        auto base = parseLevel("global", global);
        cfg.log.set_all(base);

        if (result.count("log-sdr"))
            cfg.log.sdr = parseLevel("sdr", result["log-sdr"].as<std::string>());
        if (result.count("log-tun"))
            cfg.log.tun = parseLevel("tun", result["log-tun"].as<std::string>());
        if (result.count("log-gui"))
            cfg.log.gui = parseLevel("gui", result["log-gui"].as<std::string>());
        if (result.count("log-dsp"))
            cfg.log.dsp = parseLevel("dsp", result["log-dsp"].as<std::string>());
        if (result.count("log-main"))
            cfg.log.main = parseLevel("main", result["log-main"].as<std::string>());
        if (result.count("log-socket"))
            cfg.log.socket = parseLevel("socket", result["log-socket"].as<std::string>());
    }
    catch (const std::invalid_argument &e)
    {
        logs::main.critical("{}", e.what());
        return std::nullopt;
    }

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

    if (result.count("ip") && !result.count("n"))
        cfg.ip = result["ip"].as<std::string>();

    return cfg;
}

void set_cli_opts(SharedData &data, CliConfig &cfg)
{
    data.dsp.ofdm_cfg.mod = cfg.modulation;

    logs::sdr.set_level(cfg.log.sdr);
    logs::tun.set_level(cfg.log.tun);
    logs::gui.set_level(cfg.log.gui);
    logs::dsp.set_level(cfg.log.dsp);
    logs::main.set_level(cfg.log.main);
    logs::socket.set_level(cfg.log.socket);

    if (cfg.node == Node::A)
    {
        logs::main.info("Starting node [{}]", fmt::format(fmt::fg(fmt::color::orange), "A"));
        data.sdr.set_rx_freq(2200e6);
        data.sdr.set_tx_freq(2230e6);
        data.ip_addr = "10.0.0.1";
    }
    else if (cfg.node == Node::B)
    {
        logs::main.info("Starting node [{}]", fmt::format(fmt::fg(fmt::color::orange), "B"));
        data.sdr.set_rx_freq(2230e6);
        data.sdr.set_tx_freq(2200e6);
        data.ip_addr = "10.0.0.2";
    }
    logs::dsp.info("Set {} modulation", mod_to_string(cfg.modulation));

    if (cfg.rx_freq)
        data.sdr.set_rx_freq(*cfg.rx_freq);
    if (cfg.tx_freq)
        data.sdr.set_tx_freq(*cfg.tx_freq);

    if (cfg.ip)
        data.ip_addr = *cfg.ip;
}

#include "logger.hpp"
#include "config.hpp"

#include <fstream>

std::unordered_map<std::string, std::string> load_config(const std::filesystem::path &path)
{
    std::unordered_map<std::string, std::string> result;
    std::ifstream file(path);

    if (!file)
    {
        logs::main.warn("Config file '{}' not found, skipping", path.string());
        return result;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line))
    {
        ++line_num;

        if (line.empty() || line[0] == '#')
            continue;

        auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            logs::main.warn("Config line {}: invalid format '{}', skipping", line_num, line);
            continue;
        }

        auto key = line.substr(0, eq);
        auto val = line.substr(eq + 1);
        auto trim = [](std::string &s)
        {
            s.erase(0, s.find_first_not_of(" \t"));
            s.erase(s.find_last_not_of(" \t") + 1);
        };
        trim(key);
        trim(val);

        result[key] = val;
    }

    logs::main.debug("Loaded {} entries from '{}'", result.size(), path.string());
    return result;
}

std::vector<std::string> config_to_args(const std::unordered_map<std::string, std::string> &cfg)
{
    std::vector<std::string> args;
    for (const auto &[key, val] : cfg)
        args.push_back("--" + key + "=" + val);
    return args;
}
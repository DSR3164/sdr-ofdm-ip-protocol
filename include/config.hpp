#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

std::unordered_map<std::string, std::string> load_config(const std::filesystem::path &path);
std::vector<std::string> config_to_args(const std::unordered_map<std::string, std::string> &cfg);
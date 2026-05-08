#pragma once

#include <optional>
#include <string>

int allocate_tun(char *dev);
std::optional<std::string> set_interface_ip(const char *dev_name, std::string ip_addr);
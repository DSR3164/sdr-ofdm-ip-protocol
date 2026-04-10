#pragma once

#include <cstdint>
#include <optional>
#include <string>

int allocate_tun(char *dev);
std::optional<std::string> set_interface_ip(const char *dev_name, uint8_t node_id);
uint8_t node_id_prompt();
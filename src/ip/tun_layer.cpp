#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <iostream>
#include "logger.hpp"

uint8_t node_id_prompt(){
    std::cout << "Enter node ID (10.0.0.x, x=): ";
    std::string input;
    std::getline(std::cin, input);

    uint8_t id = 1;
    if (!input.empty())
        id = static_cast<uint8_t>(std::stoi(input));

    std::cout << "→ Using IP: 10.0.0." << static_cast<int>(id) << "\n";

    return id;
}

int allocate_tun(char *dev)
{
    struct ifreq ifr;

    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        logs::tun.critical("Failed to open /dev/net/tun: {} (errno {})", strerror(errno), errno);
        return fd;
    }

    logs::tun.info("TUN device opened successfully, fd: {}", fd);

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (*dev)
    {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    int err = ioctl(fd, TUNSETIFF, (void *)&ifr);
    if (err < 0)
    {
        logs::tun.critical("ioctl(TUNSETIFF) failed: {} (errno {})", strerror(errno), errno);
        close(fd);
        return err;
    }

    logs::tun.info("TUN interface allocated: {}", ifr.ifr_name);

    strcpy(dev, ifr.ifr_name);
    return fd;
}

std::optional<std::string> set_interface_ip(const char *dev_name, uint8_t node_id)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logs::tun.error("Socket creation failed: {}", strerror(errno));
        return std::nullopt;
    }

    struct ifreq ifr{};
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    char ip[INET_ADDRSTRLEN];
    snprintf(ip, sizeof(ip), "10.0.0.%d", node_id);

    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;
    addr->sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr->sin_addr);

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0) {
        logs::tun.error("SIOCSIFADDR failed: {}", strerror(errno));
        close(sock);
        return std::nullopt;
    }
    logs::tun.info("IP {} assigned to {}", ip, dev_name);

    struct sockaddr_in *netmask = (struct sockaddr_in *)&ifr.ifr_netmask;
    netmask->sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.252", &netmask->sin_addr);

    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0) {
        logs::tun.error("SIOCSIFNETMASK failed: {}", strerror(errno));
    }

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0) {
        logs::tun.error("SIOCGIFFLAGS failed: {}", strerror(errno));
    } else {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0) {
            logs::tun.error("SIOCSIFFLAGS failed: {}", strerror(errno));
        } else {
            logs::tun.info("Interface {} is UP", dev_name);
        }
    }

    close(sock);
    return std::string(ip);
}

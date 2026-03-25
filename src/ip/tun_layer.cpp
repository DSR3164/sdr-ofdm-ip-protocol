#include "ip/ip_layer.hpp"
#include <spdlog/spdlog.h>

int allocate_tun(char *dev)
{
    struct ifreq ifr;

    int fd = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd < 0)
    {
        spdlog::critical("Failed to open /dev/net/tun: {} (errno {})", strerror(errno), errno);
        return fd;
    }

    spdlog::info("TUN device opened successfully, fd: {}", fd);

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;
    if (*dev)
    {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    int err = ioctl(fd, TUNSETIFF, (void *)&ifr);
    if (err < 0)
    {
        spdlog::critical("ioctl(TUNSETIFF) failed: {} (errno {})", strerror(errno), errno);
        close(fd);
        return err;
    }

    spdlog::info("TUN interface allocated: {}", ifr.ifr_name);

    strcpy(dev, ifr.ifr_name);
    return fd;
}

std::optional<std::string> set_interface_ip(const char *dev_name)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return std::nullopt;

    struct ifreq ifr{};
    struct sockaddr_in *addr = (struct sockaddr_in *)&ifr.ifr_addr;

    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    int id = 0;
    sscanf(dev_name, "tun%d", &id);

    char ip[INET_ADDRSTRLEN];
    snprintf(ip, sizeof(ip), "10.0.0.%d", id + 1);

    addr->sin_family = AF_INET;
    inet_pton(AF_INET, ip, &addr->sin_addr);

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0)
    {
        spdlog::error("Failed to assign IP {} to {}: {}", ip, dev_name, strerror(errno));
        close(sock);
        return std::nullopt;
    }

    spdlog::info("Assigned IP {} to {}", ip, dev_name);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        spdlog::error("ioctl(SIOCGIFFLAGS) failed: {}", strerror(errno));
    }
    else
    {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
            spdlog::error("ioctl(SIOCSIFFLAGS) failed: {}", strerror(errno));
        else
            spdlog::info("Interface {} is now UP and RUNNING", dev_name);
    }

    close(sock);
    return std::string(ip);
}
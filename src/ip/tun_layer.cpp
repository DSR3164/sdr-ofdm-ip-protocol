#include "ip/ip_layer.hpp"

#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>

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

std::optional<std::string> set_interface_ip(const char *dev_name)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        logs::tun.error("Socket creation failed: {}", strerror(errno));
        return std::nullopt;
    }

    struct ifreq ifr{};
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, dev_name, IFNAMSIZ - 1);

    int id = 0;
    if (sscanf(dev_name, "tun%d", &id) != 1) {
        id = 0;
    }
    char ip[INET_ADDRSTRLEN];
    snprintf(ip, sizeof(ip), "10.0.0.%d", id + 1);

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

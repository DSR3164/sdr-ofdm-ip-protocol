#include "logger.hpp"

#include <arpa/inet.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <iostream>
#ifdef __APPLE__
#include <net/if.h>
#include <net/if_utun.h>
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#else
#include <linux/if.h>
#include <linux/if_tun.h>
#endif
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>

uint8_t node_id_prompt()
{
    std::cout << "Enter node ID (10.0.0.x, x=): ";
    std::string input;
    std::getline(std::cin, input);

    uint8_t id = 1;
    if (!input.empty())
        id = static_cast<uint8_t>(std::stoi(input));

    logs::tun.info("Using IP: 10.0.0.{}", static_cast<int>(id));

    return id;
}

int allocate_tun(char *dev)
{
#ifdef __APPLE__
    struct sockaddr_ctl addr{};
    struct ctl_info info{};

    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0)
    {
        logs::tun.critical("Failed to open utun socket: {} (errno {})", strerror(errno), errno);
        return fd;
    }

    memset(&info, 0, sizeof(info));
    strncpy(info.ctl_name, UTUN_CONTROL_NAME, MAX_KCTL_NAME);

    if (ioctl(fd, CTLIOCGINFO, &info) < 0)
    {
        logs::tun.critical("ioctl(CTLIOCGINFO) failed: {}", strerror(errno));
        close(fd);
        return -1;
    }

    addr.sc_len     = sizeof(addr);
    addr.sc_family  = AF_SYSTEM;
    addr.ss_sysaddr = AF_SYS_CONTROL;
    addr.sc_id      = info.ctl_id;
    addr.sc_unit    = 0;

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0)
    {
        logs::tun.critical("connect() failed: {}", strerror(errno));
        close(fd);
        return -1;
    }

    char ifname[IFNAMSIZ];
    socklen_t ifname_len = sizeof(ifname);
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &ifname_len) < 0)
    {
        logs::tun.critical("getsockopt(UTUN_OPT_IFNAME) failed: {}", strerror(errno));
        close(fd);
        return -1;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    logs::tun.info("TUN interface allocated: {}", ifname);
    strcpy(dev, ifname);
    return fd;

#else
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
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);

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
#endif
}

std::optional<std::string> set_interface_ip(const char *dev_name, uint8_t node_id)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
    {
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

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0)
    {
        logs::tun.error("SIOCSIFADDR failed: {}", strerror(errno));
        close(sock);
        return std::nullopt;
    }
    logs::tun.info("IP {} assigned to {}", ip, dev_name);

    #ifndef __APPLE__
    struct sockaddr_in *netmask = (struct sockaddr_in *)&ifr.ifr_netmask;
    netmask->sin_family = AF_INET;
    inet_pton(AF_INET, "255.255.255.252", &netmask->sin_addr);

    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0)
        logs::tun.error("SIOCSIFNETMASK failed: {}", strerror(errno));
    #endif

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
        logs::tun.error("SIOCGIFFLAGS failed: {}", strerror(errno));
    else
    {
        ifr.ifr_flags |= IFF_UP | IFF_RUNNING;
        if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
            logs::tun.error("SIOCSIFFLAGS failed: {}", strerror(errno));
        else
            logs::tun.info("Interface {} is UP", dev_name);
    }

    close(sock);
    return std::string(ip);
}

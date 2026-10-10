#include "ip/tun_layer.hpp"

TunDevice::TunDevice(std::string name)
{
    fd_ = open("/dev/net/tun", O_RDWR | O_NONBLOCK);
    if (fd_ < 0)
    {
        logs::tun.critical("Failed to open /dev/net/tun: {} (errno {})", strerror(errno), errno);
        return;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;

    if (!name.empty())
        strncpy(ifr.ifr_name, name.c_str(), IFNAMSIZ - 1);

    if (ioctl(fd_, TUNSETIFF, static_cast<void *>(&ifr)) < 0)
    {
        logs::tun.critical("ioctl(TUNSETIFF) failed on {}: {} (errno {})", name, strerror(errno), errno);
        close_fd();
        return;
    }

    name_ = ifr.ifr_name;
    logs::tun.info("TUN interface allocated: {}, fd: {}", name_, fd_);
}

bool TunDevice::set_ip_and_up(const std::string &ip, const std::string &netmask)
{
    if (fd_ < 0)
    {
        logs::tun.error("Cannot configure IP: TUN device is not initialized");
        return false;
    }

    struct SockCloser {
        int fd{ -1 };
        ~SockCloser()
        {
            if (fd >= 0)
                close(fd);
        }
    } sock_guard{ socket(AF_INET, SOCK_DGRAM, 0) };

    if (sock_guard.fd < 0)
    {
        logs::tun.error("Failed to open socket for ioctl: {} (errno {})", strerror(errno), errno);
        return false;
    }

    int sock = sock_guard.fd;
    struct ifreq ifr;
    struct sockaddr_in *sin = reinterpret_cast<struct sockaddr_in *>(&ifr.ifr_addr);

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, get_name().c_str(), IFNAMSIZ - 1);

    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, ip.c_str(), &sin->sin_addr) <= 0)
    {
        logs::tun.error("Invalid IP address format: {}", ip);
        return false;
    }

    if (ioctl(sock, SIOCSIFADDR, &ifr) < 0)
    {
        logs::tun.error("SIOCSIFADDR failed for {}: {} (errno {})", name_, strerror(errno), errno);
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, get_name().c_str(), IFNAMSIZ - 1);

    sin->sin_family = AF_INET;
    if (inet_pton(AF_INET, netmask.c_str(), &sin->sin_addr) <= 0)
    {
        logs::tun.error("Invalid NetMask format: {}", netmask);
        return false;
    }

    if (ioctl(sock, SIOCSIFNETMASK, &ifr) < 0)
    {
        logs::tun.error("SIOCSIFNETMASK failed for {}: {} (errno {})", name_, strerror(errno), errno);
        return false;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, get_name().c_str(), IFNAMSIZ - 1);

    if (ioctl(sock, SIOCGIFFLAGS, &ifr) < 0)
    {
        logs::tun.error("SIOCGIFFLAGS failed on {}: {} (errno {})", name_, strerror(errno), errno);
        return false;
    }

    ifr.ifr_flags |= (IFF_UP | IFF_RUNNING);

    if (ioctl(sock, SIOCSIFFLAGS, &ifr) < 0)
    {
        logs::tun.error("SIOCSIFFLAGS failed on {}: {} (errno {})", name_, strerror(errno), errno);
        return false;
    }

    logs::tun.info("Interface {} configured with IP {}/{} and brought UP", name_, ip, netmask);
    return true;
}

bool TunDevice::set_mtu(int mtu)
{
    if (fd_ < 0)
    {
        logs::tun.error("Cannot set MTU: TUN device is not initialized");
        return false;
    }

    struct SockCloser {
        int fd{ -1 };
        ~SockCloser()
        {
            if (fd >= 0)
                close(fd);
        }
    } sock_guard{ socket(AF_INET, SOCK_DGRAM, 0) };

    if (sock_guard.fd < 0)
    {
        logs::tun.error("Failed to open socket for set_mtu: {} (errno {})", strerror(errno), errno);
        return false;
    }

    struct ifreq ifr;
    std::memset(&ifr, 0, sizeof(ifr));
    std::strncpy(ifr.ifr_name, name_.c_str(), IFNAMSIZ - 1);
    ifr.ifr_mtu = mtu;

    if (ioctl(sock_guard.fd, SIOCSIFMTU, &ifr) < 0)
    {
        logs::tun.error("SIOCSIFMTU failed on {} (MTU={}): {} (errno {})", name_, mtu, strerror(errno), errno);
        return false;
    }

    logs::tun.info("MTU set to {} on {}", mtu, name_);
    return true;
}

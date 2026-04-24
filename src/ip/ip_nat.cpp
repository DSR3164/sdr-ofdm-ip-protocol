#include "logger.hpp"
#include "ip/ip_nat.hpp"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <net/if.h>
#include <net/route.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static void fill_sockaddr(sockaddr &sa, const char *ip)
{
    auto *s = reinterpret_cast<sockaddr_in *>(&sa);
    s->sin_family = AF_INET;
    inet_pton(AF_INET, ip, &s->sin_addr);
}

static bool run_cmd(const char *path, const char *const args[])
{
    pid_t pid = fork();
    if (pid == 0)
    {
        execv(path, const_cast<char *const *>(args));
        _exit(1);
    }
    if (pid < 0)
    {
        logs::tun.error("Fork failed");
        return 1;
    }
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static std::string default_iface()
{
    std::ifstream f("/proc/net/route");
    std::string line;
    std::getline(f, line);
    while (std::getline(f, line))
    {
        char dev[64];
        unsigned long dest, gw, flags;
        sscanf(line.c_str(), "%63s %lX %lX %lX", dev, &dest, &gw, &flags);
        if (dest == 0 && (flags & 0x2))
            return dev;
    }
    return "";
}

bool enable_nat(const std::string &tun_name)
{
    int fd = open("/proc/sys/net/ipv4/ip_forward", O_WRONLY);
    if (fd < 0 || write(fd, "1", 1) != 1)
    {
        logs::tun.error("ip_forward failed");
        return false;
    }
    close(fd);

    auto iface = default_iface();
    if (iface.empty())
    {
        logs::tun.error("Cannot detect default iface");
        return false;
    }

    const char *del[] = { "/sbin/iptables", "-t", "nat", "-D", "POSTROUTING", "-o", iface.c_str(), "-j", "MASQUERADE", nullptr };
    run_cmd("/sbin/iptables", del);

    const char *del_fwd_in[] = { "/sbin/iptables", "-D", "FORWARD", "-i", tun_name.c_str(), "-j", "ACCEPT", nullptr };
    run_cmd("/sbin/iptables", del_fwd_in);

    const char *del_fwd_out[] = { "/sbin/iptables", "-D", "FORWARD", "-o", tun_name.c_str(), "-m", "state", "--state", "RELATED,ESTABLISHED", "-j", "ACCEPT", nullptr };
    run_cmd("/sbin/iptables", del_fwd_out);

    const char *add[] = { "/sbin/iptables", "-t", "nat", "-A", "POSTROUTING", "-o", iface.c_str(), "-j", "MASQUERADE", nullptr };
    if (!run_cmd("/sbin/iptables", add))
    {
        logs::tun.error("iptables masquerade failed");
        return false;
    }

    const char *fwd_in[] = { "/sbin/iptables", "-A", "FORWARD", "-i", tun_name.c_str(), "-j", "ACCEPT", nullptr };
    if (!run_cmd("/sbin/iptables", fwd_in))
    {
        logs::tun.error("iptables FORWARD -i failed");
        return false;
    }

    const char *fwd_out[] = { "/sbin/iptables", "-A", "FORWARD", "-o", tun_name.c_str(), "-m", "state", "--state", "RELATED,ESTABLISHED", "-j", "ACCEPT", nullptr };
    if (!run_cmd("/sbin/iptables", fwd_out))
    {
        logs::tun.error("iptables FORWARD -o failed");
        return false;
    }

    logs::tun.info("NAT enabled: {} -> {}", tun_name, iface);
    return true;
}

bool enable_client(const std::string &tun_name)
{
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        return false;

    rtentry route{};
    fill_sockaddr(route.rt_dst, "0.0.0.0");
    fill_sockaddr(route.rt_genmask, "0.0.0.0");
    fill_sockaddr(route.rt_gateway, "10.0.0.1");

    route.rt_flags = RTF_UP | RTF_GATEWAY;
    route.rt_metric = 100;

    char dev[IFNAMSIZ];
    strncpy(dev, tun_name.c_str(), IFNAMSIZ - 1);
    route.rt_dev = dev;

    if (ioctl(sock, SIOCADDRT, &route) < 0 && errno != EEXIST)
    {
        logs::tun.error("SIOCADDRT failed: {}", strerror(errno));
        close(sock);
        return false;
    }
    close(sock);

    std::ofstream("/etc/resolv.conf", std::ios::trunc) << "nameserver 8.8.8.8\n";

    logs::tun.info("Client route + DNS configured via {}", tun_name);
    return true;
}
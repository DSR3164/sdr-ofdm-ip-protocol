#pragma once
#include "logger.hpp"

#include <arpa/inet.h>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <netinet/in.h>
#include <string>
#include <sys/ioctl.h>

class TunDevice {
  private:
    int fd_{ -1 };
    std::string name_;

    void close_fd() noexcept
    {
        if (fd_ >= 0)
        {
            logs::tun.debug("Closing TUN device {}, fd: {}", name_, fd_);
            close(fd_);
            fd_ = -1;
        }
    }
  public:
    explicit TunDevice(std::string name);

    ~TunDevice()
    {
        close_fd();
    }

    TunDevice(const TunDevice &) = delete;
    TunDevice &operator=(const TunDevice &) = delete;

    TunDevice(TunDevice &&other) noexcept
    {
        fd_ = other.fd_;
        other.fd_ = -1;

        name_ = std::move(other.name_);
    }

    TunDevice &operator=(TunDevice &&other) noexcept
    {
        if (this == &other)
            return *this;

        if (fd_ >= 0)
            close(fd_);
        fd_ = other.fd_;
        other.fd_ = -1;
        name_ = std::move(other.name_);

        return *this;
    }

    bool set_ip_and_up(const std::string &ip, const std::string &netmask);

    bool set_mtu(int mtu);

    ssize_t read_packet(uint8_t *buffer, size_t max_len) const
    {
        return ::read(fd_, buffer, max_len);
    }

    ssize_t write_packet(const uint8_t *buffer, size_t len) const
    {
        return ::write(fd_, buffer, len);
    }

    int get_fd() const noexcept { return fd_; }
    const std::string &get_name() const noexcept { return name_; }
};

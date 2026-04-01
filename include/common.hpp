#pragma once

#include "phy/sdr.hpp"
#include <vector>
#include <cmath>
#include <atomic>
#include <cstdint>
#include <sys/socket.h>
#include <sys/un.h>
#include <string>
#include <cstring>
#include <utility>
#include <unistd.h>
#include <chrono>

enum class MsgType : uint32_t {
    Flag,
    Spectrum,
    BER,
    Status,
    Error
};

enum class Modulation
{
    BPSK,
    QPSK,
    QAM16,
    QAM64,
};

struct ipc_header
{
    MsgType type;
    uint32_t size;
    uint64_t timestamp_ns;
};

struct ipc_frame
{
    ipc_header header;
    char payload[];
};

struct DSP {
    float cfo = 0.0f;
    int max_index = 0;
    float sample_rate = 1.92e6;
    struct OFDMConfig {
        Modulation mod = Modulation::QAM16;
        int n_subcarriers = 128;
        int pilot_spacing = 6;
        int n_cp = 32;
    } ofdm_cfg;
};

template <typename T>
class DoubleBuffer {
    public:
    DoubleBuffer(size_t reserve_size = 1920*2)
    {
        buff[0].resize(reserve_size);
        buff[1].resize(reserve_size);
    }
    ~DoubleBuffer() = default;

    int read(std::vector<T> &buffer)
    {
        if (!ready.load(std::memory_order_acquire))
            return -1;
        else
        {
            int ri = read_index.load(std::memory_order_relaxed);
            buffer = buff[ri];
            ready.store(false, std::memory_order_relaxed);
            return 0;
        }
    }
    int write(std::vector<T> &buffer)
    {
        int wi = write_index.load(std::memory_order_relaxed);
        buff[wi] = buffer;
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return 0;
    }
    std::vector<T> &get_write_buffer()
    {
        int index = write_index.load(std::memory_order_relaxed);
        return buff[index];
    }
    int swap()
    {
        int wi = write_index.load(std::memory_order_relaxed);
        read_index.store(wi, std::memory_order_relaxed);
        write_index.store(wi ^ 1, std::memory_order_relaxed);
        ready.store(true, std::memory_order_release);
        return 0;
    }
    bool is_ready() const
    {
        return ready.load(std::memory_order_relaxed);
    }
    private:
    std::vector<T> buff[2];
    std::atomic<int> write_index{ 0 };
    std::atomic<int> read_index{ 1 };
    std::atomic<bool> ready{ false };
};

class IPC {
    private:
    int _socket_fd = -1;
    int _listen_fd = -1;
    std::vector<std::byte> _frame_buf;

    // Безопасное чтение ровно N байт из сокета
    bool recv_exact(void *dst, size_t n)
    {
        auto *p = static_cast<char *>(dst);
        size_t received = 0;
        while (received < n)
        {
            ssize_t r = recv(_socket_fd, p + received, n - received, 0);
            if (r <= 0) return false;
            received += static_cast<size_t>(r);
        }
        return true;
    }

    // Безопасная полная отправка буфера
    bool send_exact(const void *src, size_t n)
    {
        const auto *p = static_cast<const char *>(src);
        size_t sent = 0;
        while (sent < n)
        {
            ssize_t r = send(_socket_fd, p + sent, n - sent, MSG_NOSIGNAL);
            if (r <= 0) return false;
            sent += static_cast<size_t>(r);
        }
        return true;
    }

    // Указатель на header внутри буфера
    ipc_header *header_ptr()
    {
        return std::launder(reinterpret_cast<ipc_header *>(_frame_buf.data()));
    }
    const ipc_header *header_ptr() const
    {
        return std::launder(reinterpret_cast<const ipc_header *>(_frame_buf.data()));
    }

    public:
    IPC() = default;

    // Rule of five: запрещаем копирование, разрешаем перемещение
    IPC(const IPC &) = delete;
    IPC &operator=(const IPC &) = delete;

    IPC(IPC &&other) noexcept
        : _socket_fd(std::exchange(other._socket_fd, -1))
        , _listen_fd(std::exchange(other._listen_fd, -1))
        , _frame_buf(std::move(other._frame_buf))
    {
    }

    IPC &operator=(IPC &&other) noexcept
    {
        if (this != &other)
        {
            if (_socket_fd != -1) close(_socket_fd);
            if (_listen_fd != -1) close(_listen_fd);
            _socket_fd = std::exchange(other._socket_fd, -1);
            _listen_fd = std::exchange(other._listen_fd, -1);
            _frame_buf = std::move(other._frame_buf);
        }
        return *this;
    }


    ~IPC()
    {
        if (_socket_fd != -1) close(_socket_fd);
        if (_listen_fd != -1) close(_listen_fd);
    }

    static int64_t now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()
        ).count();
    }

    // --- socket setup ---

    int create_socket(const std::string &path)
    {
        _listen_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (_listen_fd == -1) return -1;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        unlink(path.c_str());
        if (bind(_listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
        {
            close(_listen_fd); _listen_fd = -1; return -1;
        }
        listen(_listen_fd, 5);
        return 0;
    }

    int connect_to_socket(const std::string &path)
    {
        _socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (_socket_fd == -1) return -1;

        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (connect(_socket_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) == -1)
        {
            close(_socket_fd); _socket_fd = -1; return -1;
        }
        return 0;
    }

    int set_socket()
    {
        int fd = accept(_listen_fd, nullptr, nullptr);
        if (fd == -1) return -1;
        if (_socket_fd != -1) close(_socket_fd);
        _socket_fd = fd;
        return 0;
    }

    int get_fd() const { return _socket_fd; }

    // --- frame construction ---

    // Из вектора T
    template<typename T>
    void create_msg(const std::vector<T> &data)
    {
        auto payload = std::as_bytes(std::span(data));
        _frame_buf.resize(sizeof(ipc_header) + payload.size());

        auto &hdr = *header_ptr();
        hdr.type = MsgType::Spectrum;
        hdr.size = static_cast<uint32_t>(payload.size());
        hdr.timestamp_ns = now_ns();

        std::ranges::copy(payload, _frame_buf.begin() + sizeof(ipc_header));
    }

    // Из готового фрейма (копирование с правильным размером)
    void create_msg(const ipc_frame &frame)
    {
        size_t total = sizeof(ipc_header) + frame.header.size;
        auto src = std::as_bytes(std::span(&frame, 1)).first(total);
        _frame_buf.assign(src.begin(), src.end());
    }

    // --- send / recv ---

    bool send_frame()
    {
        if (_socket_fd == -1 || _frame_buf.empty()) return false;
        return send_exact(_frame_buf.data(), _frame_buf.size());
    }

    // Неблокирующий recv: читает header peek-ом, затем весь фрейм
    bool recv_frame()
    {
        ipc_header h{};
        ssize_t r = recv(_socket_fd, &h, sizeof(h), MSG_PEEK | MSG_DONTWAIT);
        if (r <= 0) return false;

        size_t total = sizeof(ipc_header) + h.size;
        _frame_buf.resize(total);
        r = recv(_socket_fd, _frame_buf.data(), total, MSG_WAITALL);
        if (r <= 0) { _frame_buf.clear(); return false; }
        return true;
    }

    // Блокирующий recv с разбором в типизированный вектор
    template<typename T>
    bool recv_frame(ipc_header &out_header, std::vector<T> &out_data)
    {
        if (!recv_exact(&out_header, sizeof(out_header))) return false;

        size_t n_elements = out_header.size / sizeof(T);
        out_data.resize(n_elements);

        auto dst = std::as_writable_bytes(std::span(out_data));
        return recv_exact(dst.data(), out_header.size);
    }

    // Доступ к текущему фрейму
    std::span<const std::byte> frame_bytes() const { return _frame_buf; }

    const ipc_header *frame_header() const
    {
        if (_frame_buf.size() < sizeof(ipc_header)) return nullptr;
        return header_ptr();
    }

    template<typename T>
    std::span<const T> frame_payload() const
    {
        if (_frame_buf.size() <= sizeof(ipc_header)) return {};
        auto payload = std::span(_frame_buf).subspan(sizeof(ipc_header));
        return { reinterpret_cast<const T *>(payload.data()), payload.size() / sizeof(T) };
    }
};

struct SharedData
{
    SDR sdr;
    DSP dsp;

    DoubleBuffer<uint8_t> ip_phy;
    DoubleBuffer<uint8_t> phy_ip;

    DoubleBuffer<int16_t> sdr_dsp_tx;
    DoubleBuffer<int16_t> sdr_dsp_rx;

    DoubleBuffer<std::complex<float>> dsp_sockets;
    DoubleBuffer<uint8_t> ip_sockets_bytes;

    SharedData() : sdr(SDRConfig{}), dsp_sockets(SDRConfig{}.buffer_size * 2) {}
};

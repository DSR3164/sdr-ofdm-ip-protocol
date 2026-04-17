#pragma once

#include "logger.hpp"

#include "zmq.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>

struct socketData
{
    std::string socketPath;
    std::string socketPid;
    std::string ip_socket;
    std::string phy_socket;
    bool is_owner = false;

    socketData(const bool setup_dir = true, const std::string &base_folder = "soip_sockets");
    ~socketData();
    std::string setup_socket_dir(const std::string &folder_name);
};

void found_sockets(std::vector<std::string> &sockets, const std::string base_name = "soip_sockets_");

enum class MsgType : uint32_t
{
    Flag,
    Spectrum,
    BER,
    Status,
    Error,
    Vector,
    pid,
    var,
};

struct ipc_header
{
    MsgType type;
    uint32_t size;
    uint64_t timestamp_ns;
};

class IPC
{
  private:
    zmq::context_t _context;
    zmq::socket_t _socket;

  public:
    IPC() : _context(1), _socket(_context, zmq::socket_type::pub) {}

    bool start_server(const std::string &path);
    bool connect_to(const std::string &path);

    bool recv_header(ipc_header &header, bool &has_more);

    static uint64_t now_ns()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    template <typename T> bool send_frame(MsgType type, const std::vector<T> &data)
    {
        ipc_header hdr;
        hdr.type = type;
        hdr.size = static_cast<uint32_t>(data.size() * sizeof(T));
        hdr.timestamp_ns = now_ns();

        try
        {
            _socket.send(zmq::buffer(&hdr, sizeof(hdr)), zmq::send_flags::sndmore);
            auto send_state = _socket.send(zmq::buffer(data.data(), hdr.size), zmq::send_flags::none);

            return send_state.has_value();
        }
        catch (const zmq::error_t &e)
        {
            logs::socket.error("ZMQ Error: {} (Code: {})", e.what(), e.num());
            return false;
        }
    }

    template <typename T> bool send_value(MsgType type, const T &data)
    {
        ipc_header hdr;
        hdr.type = type;
        hdr.size = static_cast<uint32_t>(sizeof(data));
        hdr.timestamp_ns = now_ns();

        try
        {
            _socket.send(zmq::buffer(&hdr, sizeof(hdr)), zmq::send_flags::sndmore);
            auto send_state = _socket.send(zmq::buffer(&data, hdr.size), zmq::send_flags::none);

            return send_state.has_value();
        }
        catch (const zmq::error_t &e)
        {
            logs::socket.error("ZMQ Error: {} (Code: {})", e.what(), e.num());
            return false;
        }
    }

    template <typename T>
    bool recv_payload_vector(std::vector<T> &data) {
        zmq::message_t msg_p;
        auto res = _socket.recv(msg_p, zmq::recv_flags::none);
        if (!res) return false;

        size_t n_elements = msg_p.size() / sizeof(T);
        data.resize(n_elements);
        std::memcpy(data.data(), msg_p.data(), msg_p.size());
        return true;
    }

    template <typename T>
    bool recv_payload_value(T &data) {
        zmq::message_t msg_p;
        auto res = _socket.recv(msg_p, zmq::recv_flags::none);
        if (!res || msg_p.size() != sizeof(T)) return false;

        std::memcpy(&data, msg_p.data(), sizeof(T));
        return true;
    }

    zmq::socket_t &socket() { return _socket; }
};

int run_gui_main(socketData &socket);

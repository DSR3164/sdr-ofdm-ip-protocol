#include "ip/utils.hpp"
#include "bridges.hpp"

#include <arpa/inet.h>

extern float lat;

std::atomic_bool quit{ false };

// Backend Bridges
int run_control_bridge_client(SharedData &data, socketData &socket)
{
    while (socket.control_socket.empty())
    {
        if (!data.stop.load())
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string last_connected_path = "";
    IPC client(zmq::socket_type::sub);
    ipc_header header;
    bool init = false;
    std::vector<ControlSignals> c_signals(1);

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (!data.stop.load())
    {
        if (socket.control_socket != last_connected_path && !socket.control_socket.empty())
        {
            init = false;
            if (!last_connected_path.empty())
                client.disconnect_from(last_connected_path);
            last_connected_path = socket.control_socket;
            logs::socket.info("Control bridge connecting to: {}", socket.control_socket);

            while (!init)
            {
                if (!client.connect_to(socket.control_socket))
                {
                    logs::socket.error("Failed to connect to control bridge");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else
                {
                    logs::socket.info("Backend connect to control bridge");
                    init = true;
                }
            }
        }

        if (!init)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (client.recv_frame(header, c_signals))
            data.is_gui_run.store(c_signals.back().gui_run);
    }

    return 0;
}

int run_ip_gui_bridge(SharedData &data, socketData &socket)
{
    IPC server(zmq::socket_type::pub);
    bool init = false;
    logs::tun.info("GUI bridge thread initialized");

    while (!init && !data.stop.load() && data.is_gui_run.load())
    {
        if (!server.start_server(socket.ip_socket))
        {
            logs::socket.error("Failed to start IP to GUI bridge");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else
        {
            logs::socket.info("IP to GUI bridge server started");
            init = true;
        }
    }

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto next_users_update = std::chrono::steady_clock::now();

    while (!data.stop.load() && data.is_gui_run.load())
    {
        std::vector<uint8_t> bytes;

        if (data.ip_sockets_bytes.read(bytes) == 0)
        {
            if (!server.send_frame(MsgType::Vector, bytes))
                logs::socket.error("IP frame send failed");
        }

        auto now = std::chrono::steady_clock::now();

        if (now >= next_users_update)
        {
            auto clients = get_clients_snapshot();

            std::vector<uint8_t> users_bytes;
            users_bytes.reserve(4 + clients.size() * 10);

            auto append_u16 = [&users_bytes](uint16_t value)
            {
                users_bytes.push_back(static_cast<uint8_t>(value >> 8));
                users_bytes.push_back(static_cast<uint8_t>(value));
            };

            auto append_u32 = [&users_bytes](uint32_t value)
            {
                users_bytes.push_back(static_cast<uint8_t>(value >> 24));
                users_bytes.push_back(static_cast<uint8_t>(value >> 16));
                users_bytes.push_back(static_cast<uint8_t>(value >> 8));
                users_bytes.push_back(static_cast<uint8_t>(value));
            };

            append_u32(static_cast<uint32_t>(clients.size()));

            for (const auto &client : clients)
            {
                append_u16(client.rnti);
                append_u32(client.user_token);
                append_u32(ntohl(client.ip));
            }

            if (!server.send_frame(MsgType::Users, users_bytes))
                logs::socket.error("Users frame send failed");

            next_users_update = now + std::chrono::seconds(1);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    logs::tun.info("GUI bridge stopped");
    return 0;
}

int run_dsp_gui_bridge(SharedData &data, socketData &socket)
{
    IPC server(zmq::socket_type::pub);
    bool init = false;
    std::vector<std::complex<float>> raw;
    std::vector<std::complex<float>> symbols;

    while (!init && !data.stop.load() && data.is_gui_run.load())
    {
        if (!server.start_server(socket.phy_socket))
        {
            logs::socket.error("Failed to start PHY to GUI bridge");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else
        {
            logs::socket.info("PHY to GUI bridge server started");
            init = true;
        }
    }

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    logs::dsp.info("Socket created successfully {}", socket.phy_socket);

    while (!data.stop.load() && data.is_gui_run.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        if (data.dsp_sockets_raw.read(raw) == 0)
            server.send_frame(MsgType::Spectrum, raw);
        if (data.dsp_sockets_symbols.read(symbols) == 0)
            server.send_frame(MsgType::Vector, symbols);
    }

    return 0;
}

int run_dsp_stats_bridge(SharedData &data, socketData &socket)
{
    IPC server(zmq::socket_type::pub);
    bool init = false;

    std::vector<uint8_t> stats(sizeof(StatsSnapshot));
    StatsSnapshot tmp;

    while (!init && !data.stop.load() && data.is_gui_run.load())
    {
        if (!server.start_server(socket.stats_socket))
        {
            logs::socket.error("Failed to start PHY Stats to GUI bridge");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else
        {
            logs::socket.info("PHY Stats to GUI bridge server started");
            init = true;
        }
    }

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    logs::dsp.info("Socket created successfully {}", socket.stats_socket);

    while (!data.stop.load() && data.is_gui_run.load())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(6));
        data.history.get_last(tmp);
        std::memcpy(stats.data(), &tmp, sizeof(tmp));
        if (!stats.empty())
            server.send_frame(MsgType::Stats, stats);
    }

    return 0;
}

void run_gui_bridge_supervisor(SharedData &data, socketData &socket)
{
    while (!data.stop.load())
    {
        if (data.is_gui_run.load())
        {
            {
                ThreadJoiner ip_gui_bridge{ "ip_gui_bridge", std::jthread(run_ip_gui_bridge, std::ref(data), std::ref(socket)), "main" };
                ThreadJoiner dsp_gui_bridge{ "dsp_gui_bridge", std::jthread(run_dsp_gui_bridge, std::ref(data), std::ref(socket)), "main" };
                ThreadJoiner dsp_stats_gui_bridge{ "dsp_stats_gui_bridge", std::jthread(run_dsp_stats_bridge, std::ref(data), std::ref(socket)), "main" };

                while (data.is_gui_run.load() && !data.stop.load())
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

// GUI Bridges
int run_control_bridge_server(socketData &socket)
{
    while (socket.control_socket.empty())
    {
        if (quit)
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string last_connected_path = "";
    bool init = false;
    logs::socket.info("Control bridge thread initialized");

    std::unique_ptr<IPC> server;

    while (true)
    {
        if (socket.control_socket != last_connected_path && !socket.control_socket.empty())
        {
            init = false;
            last_connected_path = socket.control_socket;
            server = std::make_unique<IPC>(zmq::socket_type::pub);
            logs::socket.info("Control bridge rebinding to: {}", socket.control_socket);

            while (!init)
            {
                if (!server->start_server(socket.control_socket))
                {
                    logs::socket.error("Failed to start control bridge");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else
                {
                    logs::socket.info("Control bridge server started");
                    init = true;
                }
            }
        }

        if (!init)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (quit)
        {
            std::vector<ControlSignals> c_signals(1, ControlSignals{ false });
            server->send_frame(MsgType::Control, c_signals);
            return 0;
        }

        std::vector<ControlSignals> c_signals(1, ControlSignals{ true });
        server->send_frame(MsgType::Control, c_signals);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    logs::socket.info("Control bridge stopped");
    return 0;
}

int run_ip_brigde(Buffers &data, socketData &socket)
{
    while (socket.ip_socket.empty())
    {
        if (quit)
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::string last_connected_path = "";
    IPC client(zmq::socket_type::sub);
    ipc_header header;
    bool init = false;
    std::vector<uint8_t> bytes;

    while (!quit)
    {
        if (socket.ip_socket != last_connected_path && !socket.ip_socket.empty())
        {
            init = false;
            if (!last_connected_path.empty())
                client.disconnect_from(last_connected_path);
            last_connected_path = socket.ip_socket;
            logs::gui.info("IP Bridge connecting to: {}", socket.ip_socket);

            while (!init)
            {
                if (!client.connect_to(socket.ip_socket))
                {
                    logs::socket.error("Failed to connect to IP bridge");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else
                {
                    logs::socket.info("GUI connect to IP bridge");
                    init = true;
                }
            }
        }

        if (!init)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (client.recv_frame(header, bytes))
        {
            switch (header.type)
            {
            case MsgType::Vector:
                data.ip.write(bytes);
                break;

            case MsgType::Users:
                data.users.write(bytes);
                break;

            default:
                break;
            }
        }
    }

    return 0;
}

int run_dsp_bridge(Buffers &bufs, socketData &socket)
{
    while (socket.phy_socket.empty())
    {
        if (quit)
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    IPC client(zmq::socket_type::sub);
    std::string last_connected_path = "";
    ipc_header h;
    bool init = false;
    std::vector<std::complex<float>> temp(1920 * 2, 0);

    while (!quit)
    {
        if (socket.phy_socket != last_connected_path && !socket.phy_socket.empty())
        {
            init = false;
            if (!last_connected_path.empty())
                client.disconnect_from(last_connected_path);
            last_connected_path = socket.phy_socket;
            logs::gui.info("PHY Bridge connecting to: {}", socket.phy_socket);

            while (!init)
            {
                if (!client.connect_to(socket.phy_socket))
                {
                    logs::socket.error("Failed to connect to PHY bridge");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else
                {
                    logs::socket.info("GUI connect to PHY bridge");
                    init = true;
                }
            }
        }

        if (!init)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (client.recv_frame(h, temp))
        {
            logs::gui.trace("RECIEVED frame from SDR, size: {}", temp.size());
            lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
            if (h.type == MsgType::Spectrum)
                bufs.sdr_raw.write(temp);
            else if (h.type == MsgType::Vector)
                bufs.dsp.write(temp);
        }
    }
    return 0;
}

int run_stats_bridge(Buffers &bufs, socketData &socket)
{
    while (socket.stats_socket.empty())
    {
        if (quit)
            return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    IPC client(zmq::socket_type::sub);
    std::string last_connected_path = "";
    ipc_header h;
    bool init = false;
    std::vector<uint8_t> temp(sizeof(StatsSnapshot));

    while (!quit)
    {
        if (socket.stats_socket != last_connected_path && !socket.stats_socket.empty())
        {
            init = false;
            if (!last_connected_path.empty())
                client.disconnect_from(last_connected_path);
            last_connected_path = socket.stats_socket;
            logs::gui.info("PHY Stats Bridge connecting to: {}", socket.stats_socket);

            while (!init)
            {
                if (!client.connect_to(socket.stats_socket))
                {
                    logs::socket.error("Failed to connect to PHY Stats bridge");
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                else
                {
                    logs::socket.info("GUI connect to PHY Stats bridge");
                    init = true;
                }
            }
        }

        if (!init)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (client.recv_frame(h, temp))
        {
            logs::gui.trace("RECIEVED frame from PHY, size: {}", temp.size());
            lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
            if (h.type == MsgType::Stats)
                bufs.stats.write(temp);
        }
    }
    return 0;
}

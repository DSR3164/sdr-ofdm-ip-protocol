#include "common.hpp"
#include "sockets.hpp"
#include "gui/app.hpp"
#include "zmq.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

float lat = 1.0f;

int run_ip_brigde(Buffers &data, socketData &socket)
{
    while (socket.ip_socket.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::string last_connected_path = "";
    IPC client(zmq::socket_type::sub);
    ipc_header header;
    bool init = false;
    static ImPlotSpec specs;
    std::vector<uint8_t> bytes;

    while (true)
    {
        if (socket.ip_socket != last_connected_path && !socket.ip_socket.empty())
        {
            init = false;
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
            data.ip.write(bytes);
    }

    return 0;
}

int run_dsp_bridge(Buffers &bufs, socketData &socket)
{
    while (socket.phy_socket.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IPC client(zmq::socket_type::sub);
    std::string last_connected_path = "";
    ipc_header h;
    bool init = false;
    std::vector<std::complex<float>> temp(1920 * 2, 0);

    while (true)
    {
        if (socket.phy_socket != last_connected_path && !socket.phy_socket.empty())
        {
            init = false;
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
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IPC client(zmq::socket_type::sub);
    std::string last_connected_path = "";
    ipc_header h;
    bool init = false;
    std::vector<uint8_t> temp(sizeof(StatsSnapshot));

    while (true)
    {
        if (socket.stats_socket != last_connected_path && !socket.stats_socket.empty())
        {
            init = false;
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

int main()
{
    std::vector<std::string> all_sockets;
    found_sockets(all_sockets);
    Buffers bufs(1920 * 2);

    socketData sock(false);

    std::thread gui(run_gui, std::ref(bufs), std::ref(all_sockets), std::ref(sock));
    std::thread phy_bridge(run_dsp_bridge, std::ref(bufs), std::ref(sock));
    std::thread stats_bridge(run_stats_bridge, std::ref(bufs), std::ref(sock));
    std::thread ip_bridge(run_ip_brigde, std::ref(bufs), std::ref(sock));

    gui.join();
    ip_bridge.join();
    phy_bridge.join();
    stats_bridge.join();

    return 0;
}

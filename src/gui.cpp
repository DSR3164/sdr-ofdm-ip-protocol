#include "sockets.hpp"
#include "gui/app.hpp"

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
    IPC client;
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

        bool has_more = false;

        while (client.recv_header(header, has_more))
        {
            if (header.type == MsgType::Vector)
                if (has_more && client.recv_payload_vector(bytes))
                    data.ip.write(bytes);

            if (header.type == MsgType::stats)
            {
                Stats s{};
                std::vector<Stats> s_vec;

                if (has_more && client.recv_payload_value(s, has_more))
                {
                    s_vec.push_back(s);
                    data.stats.write(s_vec);
                }
            }
        }
    }

    return 0;
}

int run_dsp_bridge(Buffers &bufs, socketData &socket)
{
    while (socket.phy_socket.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    IPC client;
    std::string last_connected_path = "";
    ipc_header h;
    bool init = false;
    std::vector<std::complex<float>> raw(1920 * 2);
    std::vector<std::complex<float>> symbols;

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

        bool has_more = false;

        while (client.recv_header(h, has_more))
        {
            if (h.type == MsgType::Vector)
            {
                if (has_more && client.recv_payload_vector(symbols))
                {
                    logs::gui.trace("RECIEVED frame from SDR, size: {}", symbols.size());
                    bufs.dsp.write(symbols);
                }
            }
            lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
            bufs.sdr_raw.write(raw);
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
    std::thread ip_bridge(run_ip_brigde, std::ref(bufs), std::ref(sock));

    gui.join();
    ip_bridge.join();
    phy_bridge.join();

    return 0;
}

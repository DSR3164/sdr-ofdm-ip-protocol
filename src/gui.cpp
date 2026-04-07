#include "sockets.hpp"
#include "gui/app.hpp"

#include <functional>
#include <string>
#include <thread>
#include <vector>

float lat = 1.0f;
static std::string str_pid;

int run_ip_brigde(Buffers &data, socketData &socket)
{
    while (socket.ip_socket.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    static std::string last_connected_path = "";
    IPC client;
    ipc_header header;
    static bool init = false;
    static ImPlotSpec specs;
    std::vector<uint8_t> bytes;

    while (true)
    {
        if (socket.ip_socket != last_connected_path && !socket.ip_socket.empty())
        {
            logs::gui.info("Bridge connecting to: {}", socket.ip_socket);
            client.connect_to(socket.ip_socket);
            last_connected_path = socket.ip_socket;
        }

        if (last_connected_path.empty()) {
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

    IPC client;
    static std::string last_connected_path = "";
    ipc_header h;
    std::vector<int16_t> raw(1920 * 2, 0);
    std::vector<std::complex<float>> symbols;

    while (true)
    {
        if (socket.phy_socket != last_connected_path && !socket.phy_socket.empty())
        {
            logs::gui.info("Bridge connecting to: {}", socket.phy_socket);
            client.connect_to(socket.phy_socket);
            last_connected_path = socket.phy_socket;
        }

        if (last_connected_path.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            continue;
        }

        if (client.recv_frame(h, symbols))
        {
            logs::gui.info("RECIEVED frame from SDR, size: {}", symbols.size());
            lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
            bufs.sdr_raw.write(raw);
            bufs.dsp.write(symbols);
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

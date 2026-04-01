#include "common.hpp"
#include "logger.hpp"
#include "gui/app.hpp"

#include <thread>

float lat = 1.0f;

int run_bridge(Buffers &bufs)
{
    static IPC client;
    static ipc_header h;
    static bool init = false;
    std::vector<int16_t> raw(1920 * 2, 0);
    std::vector<std::complex<float>> symbols;

    while (true)
    {
        if (!init)
        {
            while (client.connect_to_socket("/tmp/dsp_gui.sock") == -1)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            init = true;
        }
        init = client.recv_frame(h, symbols);
        lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
        bufs.sdr_raw.write(raw);
        bufs.dsp.write(symbols);
    }

    return 0;
}

int main()
{
    Buffers bufs(1920 * 2);
    std::thread bridge(run_bridge, std::ref(bufs));
    std::thread gui(run_gui, std::ref(bufs));
    gui.join();
    bridge.join();
    return 0;
}
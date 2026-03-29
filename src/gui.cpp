#include "common.hpp"
#include "gui/app.hpp"

#include <thread>

float lat = 1.0f;

int run_bridge(Buffers &bufs)
{
    IPC client;
    ipc_header h;
    bool init = false;
    std::vector<int16_t> raw;
    std::vector<std::complex<float>> symbols;

    while (true)
    {
        if (!init)
        {
            while (client.connect_to_socket("/tmp/dsp_gui.sock") == -1)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            init = true;
        }
        init = client.recv_frame(h, raw);
        lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;
        bufs.sdr_raw.write(raw);
    }

    return 0;
}

int main()
{
    Buffers bufs;
    std::thread bridge(run_bridge, std::ref(bufs));
    std::thread gui(run_gui, std::ref(bufs));
    gui.join();
    bridge.join();
    return 0;
}

#include "common.hpp"
#include "logger.hpp"
#include "phy/phy_layer.hpp"
#include "phy/dsp.hpp"
#include "ip/ip_layer.hpp"

#include <thread>
#include <fftw3.h>

int main()
{
    SharedData data;

    data.sdr.add_flag(Flags::IS_ACTIVE);
    fftwf_init_threads();
    fftwf_plan_with_nthreads(std::thread::hardware_concurrency());
    fftwf_make_planner_thread_safe();

    if (getuid() != 0)
        logs::main.critical("Please run with sudo or as root");

    std::thread dsp_rx_thread(run_dsp_rx, std::ref(data));
    std::thread dsp_tx_thread(run_dsp_tx, std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(data));
    std::thread tun_rx_thread(run_tun_rx, std::ref(data));

    std::thread dsp_gui_bridge_thread(run_dsp_gui_bridge, std::ref(data));
    std::thread ip_gui_bridge_thread(run_ip_gui_bridge, std::ref(data));

    dsp_rx_thread.join();
    dsp_tx_thread.join();
    sdr_thread.join();
    dsp_gui_bridge_thread.join();
    if (tun_rx_thread.joinable())
        tun_rx_thread.join();
    if (ip_gui_bridge_thread.joinable())
        ip_gui_bridge_thread.join();

    return 0;
}

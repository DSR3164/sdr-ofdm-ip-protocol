#include "cli.hpp"
#include "common.hpp"
#include "sockets.hpp"
#include "phy/dsp.hpp"
#include "phy/phy_layer.hpp"
#include "ip/ip_layer.hpp"

#include <atomic>
#include <csignal>
#include <fftw3.h>
#include <functional>
#include <thread>
#include <sys/capability.h>

std::atomic<bool> *stop_ptr = nullptr;
SharedData *data_ptr = nullptr;

void signal_handler(int signum)
{
    std::printf("\n");
    if (stop_ptr)
        stop_ptr->store(true);
    if (data_ptr)
        data_ptr->stop_all_buffers();
    logs::main.info("Signal {} received, stopping threads...", signum);
}

int main(int argc, char *argv[])
{
    auto cfg = parse_cli(argc, argv);
    if (!cfg)
        return 0;

    SharedData data;
    set_cli_opts(data, *cfg);

    stop_ptr = &data.stop;
    data_ptr = &data;

    std::signal(SIGINT, signal_handler);

    fftwf_init_threads();
    fftwf_plan_with_nthreads(std::thread::hardware_concurrency());
    fftwf_make_planner_thread_safe();

    if (getuid() != 0) {
        cap_t caps = cap_get_proc();
        cap_flag_value_t val = CAP_CLEAR;
        if (caps) {
            cap_get_flag(caps, CAP_NET_ADMIN, CAP_EFFECTIVE, &val);
            cap_free(caps);
        }
        if (val != CAP_SET) {
            logs::main.critical("CAP_NET_ADMIN required. Run with sudo or grant capabilities.");
            return 0;
        }
    }

    socketData socket;

    std::thread dsp_rx_thread(run_dsp_rx, std::ref(data));
    std::thread dsp_tx_thread(run_dsp_tx, std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(data));
    std::thread tun_tx_thread(run_tun_tx, std::ref(data));
    std::thread dsp_gui_bridge_thread(run_dsp_gui_bridge, std::ref(data), std::ref(socket));
    std::thread ip_gui_bridge_thread(run_ip_gui_bridge, std::ref(data), std::ref(socket));

    while (!data.stop.load())
        std::this_thread::sleep_for(std::chrono::seconds(1));

    logs::main.info("Joining dsp_rx...");
    if (dsp_rx_thread.joinable())
        dsp_rx_thread.join();

    logs::main.info("Joining dsp_tx...");
    if (dsp_tx_thread.joinable())
        dsp_tx_thread.join();

    logs::main.info("Joining sdr...");
    if (sdr_thread.joinable())
        sdr_thread.join();

    logs::main.info("Joining tun_tx...");
    if (tun_tx_thread.joinable())
        tun_tx_thread.join();

    logs::main.info("Joining dsp_bridge...");
    if (dsp_gui_bridge_thread.joinable())
        dsp_gui_bridge_thread.join();

    logs::main.info("Joining ip_bridge...");
    if (ip_gui_bridge_thread.joinable())
        ip_gui_bridge_thread.join();

    logs::main.info("All threads joined. Exiting.");
    return 0;
}

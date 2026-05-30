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

    if (getuid() != 0)
    {
        logs::main.critical("Please run with sudo or as root");
        return 0;
    }
    else
    {
        socketData socket;

        ThreadJoiner ip_gui_bridge{ "ip_gui_bridge", std::jthread(run_ip_gui_bridge, std::ref(data), std::ref(socket)) };
        ThreadJoiner dsp_gui_bridge{ "dsp_gui_bridge", std::jthread(run_dsp_gui_bridge, std::ref(data), std::ref(socket)) };
        ThreadJoiner tun_tx{ "tun_tx", std::jthread(run_tun_tx, std::ref(data)) };
        ThreadJoiner rx_thread{ "tun_rx", std::jthread(run_tun_rx, std::ref(data)) };
        ThreadJoiner sdr{ "sdr", std::jthread(run_sdr, std::ref(data)) };
        ThreadJoiner dsp_tx{ "dsp_tx", std::jthread(run_dsp_tx, std::ref(data)) };
        ThreadJoiner dsp_rx{ "dsp_rx", std::jthread(run_dsp_rx, std::ref(data)) };

        while (!data.stop.load())
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    logs::main.info("All threads joined. Exiting.");
    return 0;
}

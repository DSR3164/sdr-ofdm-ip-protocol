#include "common.hpp"
#include "phy/phy_layer.hpp"
#include "phy/dsp.hpp"

#include <thread>
#include <fftw3.h>

int main()
{
    SharedData data;

    fftwf_init_threads();
    fftwf_plan_with_nthreads(std::thread::hardware_concurrency());
    fftwf_make_planner_thread_safe();

    std::thread dsp_thread(run_dsp, std::ref(data));
    std::thread sdr_thread(run_sdr, std::ref(data));
    std::thread dsp_gui_bridge_thread(run_gui_bridge, std::ref(data));

    dsp_thread.join();
    sdr_thread.join();
    dsp_gui_bridge_thread.join();

    return 0;
}

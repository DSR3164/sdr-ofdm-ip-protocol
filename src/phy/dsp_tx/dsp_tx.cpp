#include "common.hpp"
#include "logger.hpp"
#include "phy/dsp_tx/dsp_tx.hpp"
#include "phy/dsp_tx/transmitter.hpp"
#include "phy/resource_grid.hpp"
#include "phy/scheduler.hpp"

#include <chrono>
#include <cstddef>
#include <spdlog/fmt/bundled/color.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

int run_dsp_tx(SharedData &data)
{
    pin_to_core();

    logs::dsp.info("[{}] Starting", fmt::format(fmt::fg(fmt::color::cyan), "TX"));
    std::vector<uint8_t> bits;
    std::vector<Allocation> allocation_map;
    std::vector<int16_t> buffer;

    uint64_t tx_dsp_counter = 0;

    OFDMTransmitter tx(data.dsp.ofdm_cfg);

    // Scheduler input data
    size_t num_rb = tx.get_num_rb();
    size_t num_fft = tx.get_num_fft();
    Scheduler scheduler(num_rb);
    ResourceGrid resource_grid(num_ofdm_symbols, num_rb, num_fft);

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (!data.stop.load())
    {
        // Выход с планировщика
        // allocation_map = scheduler.scheduler_output(resource_grid, data);
        // data.l2_l1.read(allocation_map, false);
        data.ip_phy.read(bits, true);
        logs::dsp.trace("Get {} bits | {} bytes from [MAC]", bits.size(), bits.size() / 8);

        tx.ofdm(bits, data.dsp.ofdm_cfg.mod, buffer);

        ++tx_dsp_counter;
        logs::dsp.trace("[{}] modulate {} samples", fmt::format(fmt::fg(fmt::color::cyan), "OFDM"), buffer.size());
        data.sdr_dsp_tx.write(buffer);
    }

    return 0;
}

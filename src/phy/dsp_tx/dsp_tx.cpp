#include "phy/dsp_tx/dsp_tx.hpp"
#include "phy/dsp_tx/transmitter.hpp"
#include "logger.hpp"

#include <chrono>
#include <spdlog/fmt/bundled/color.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

int run_dsp_tx(SharedData &data)
{
    logs::dsp.info("[{}] Starting", fmt::format(fmt::fg(fmt::color::cyan), "TX"));
    std::vector<uint8_t> bits;
    std::vector<int16_t> buffer;

    uint64_t tx_dsp_counter = 0;

    OFDMTransmitter tx(data.dsp.ofdm_cfg);

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (!data.stop.load())
    {
        data.ip_phy.read(bits, true);
        logs::dsp.trace("Get {} bits | {} bytes from [MAC]", bits.size(), bits.size() / 8);

        try
        {
            tx.ofdm(bits, data.dsp.ofdm_cfg.mod, buffer);

            ++tx_dsp_counter;
            logs::dsp.trace("[{}] modulate {} samples", fmt::format(fmt::fg(fmt::color::cyan), "OFDM"), buffer.size());
            data.sdr_dsp_tx.write(buffer);
        }
        catch (const std::exception &e)
        {
            logs::dsp.error("TX Drop: {}", e.what());
        }
    }

    return 0;
}

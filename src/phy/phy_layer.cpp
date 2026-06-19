#include "common.hpp"
#include "sockets.hpp"
#include "phy/phy_layer.hpp"
#include "phy/sdr.hpp"

#include <cstdint>
#include <cstring>
#include <vector>

int run_sdr(SharedData &data)
{
    auto &sdr = data.sdr;
    int ret_tx = data.sdr.get_buffer_size();
    std::vector<int16_t> writebuffer(data.sdr.get_buffer_size() * 2);
    Flags apply = Flags::APPLY_BANDWIDTH | Flags::APPLY_FREQUENCY | Flags::APPLY_GAIN | Flags::APPLY_SAMPLE_RATE;

    if (!has_flag(sdr.get_flags(), Flags::FOUND) && sdr.get_wait_flag())
        sdr.wait_connection();
    else if (!has_flag(sdr.get_flags(), Flags::FOUND) && !sdr.get_wait_flag())
    {
        logs::sdr.critical("No SDR devices detected, closing application");
        data.stop.store(true);
        data.stop_all_buffers();
        return 1;
    }

    if (!sdr.init())
    {
        logs::sdr.error("Initialization error");
        return -1;
    }
    std::vector<uint8_t> bits;
    data.sdr_dsp_rx.get_write_buffer().resize(data.sdr.get_buffer_size() * 2, 0);

    while (!data.stop.load())
    {
        auto ret_rx = sdr.readstream(data.sdr_dsp_rx.get_write_buffer());
        if (data.sdr_dsp_tx.read(writebuffer) == 0)
        {
            ret_tx = sdr.writestream(writebuffer);
            logs::sdr.trace("[{}] sent {} samples", fmt::format(fmt::fg(fmt::color::cyan), "TX"), ret_tx);
        }

        if (ret_rx == data.sdr.get_buffer_size())
            data.sdr_dsp_rx.swap(true);
        else if (ret_rx > 0)
            logs::sdr.warn("[{}] read only {} samples", fmt::format(fmt::fg(fmt::color::orange_red), "RX"), ret_rx);
        else if (ret_rx == SOAPY_SDR_OVERFLOW)
            logs::sdr.error("OVERFLOW");
        else
        {
            logs::sdr.warn("ERR read {}", ret_rx);
#ifdef HAS_DYNAMIC_ENUMERATE
            if (!sdr.check_connection())
#endif
            {
                data.stop.store(true);
                data.stop_all_buffers();
                return 1;
            }
        }
        if (ret_tx < sdr.get_buffer_size())
            logs::sdr.warn("ERR send {}", ret_tx);

        if (has_flag(sdr.get_flags(), Flags::REINIT))
            if (!sdr.reinit())
                logs::sdr.warn("Cannot reinit SDR");
        if (has_flag(sdr.get_flags(), apply))
            sdr.apply_runtime();
    }
    if (sdr.deinit())
        return 0;
    return 0;
}

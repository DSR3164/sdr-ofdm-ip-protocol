#include "common.hpp"
#include "logger.hpp"
#include "phy/phy_layer.hpp"
#include "phy/sdr.hpp"

#include <thread>

int run_sdr(SharedData &data)
{
    auto &sdr = data.sdr;
    Flags apply = Flags::APPLY_BANDWIDTH | Flags::APPLY_FREQUENCY | Flags::APPLY_GAIN | Flags::APPLY_SAMPLE_RATE;
    while (!has_flag(sdr.get_flags(), Flags::IS_ACTIVE))
    {
        if (has_flag(sdr.get_flags(), Flags::EXIT))
        {
            logs::sdr.info("Closing SDR thread");
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (!sdr.init())
    {
        logs::sdr.error("Initialization error");
        return -1;
    }
    std::vector<uint8_t> bits;

    while (!has_flag(sdr.get_flags(), Flags::EXIT))
    {
        auto ret_rx = sdr.readstream(data.sdr_dsp_rx.get_write_buffer());
        auto ret_tx = sdr.writestream(data.sdr_dsp_tx.get_write_buffer());

        if (ret_rx > 0)
            data.sdr_dsp_rx.swap();
        else if (ret_rx == SOAPY_SDR_OVERFLOW)
            logs::sdr.error("OVERFLOW");
        else
            logs::sdr.warn("ERR read {}", ret_rx);
        if (ret_tx < 0)
            logs::sdr.warn("ERR send {}", ret_tx);

        if (has_flag(sdr.get_flags(), Flags::REINIT))
            if (!sdr.reinit())
                logs::sdr.info("Cannot reinit SDR");
        if (has_flag(sdr.get_flags(), apply))
            sdr.apply_runtime();
    }
    if (sdr.deinit())
        return 0;
    return 0;
}

int run_gui_bridge(SharedData &data)
{
    static IPC server;
    static bool socket_init = false;

    std::vector<int16_t> temp;

    while (!has_flag(data.sdr.get_flags(), Flags::EXIT))
    {
        if (!socket_init)
        {
            while (server.create_socket("/tmp/dsp_gui.sock") == -1);
            while (server.set_socket() == -1);
            socket_init = true;
        }

        data.dsp_sockets.read(temp);

        server.create_msg(temp);
        server.send_frame();

    }

    return 0;
};


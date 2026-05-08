#include "common.hpp"
#include "sockets.hpp"
#include "phy/phy_layer.hpp"
#include "phy/sdr.hpp"

#include <thread>

int run_sdr(SharedData &data)
{
    auto &sdr = data.sdr;
    int ret_tx = data.sdr.get_buffer_size();
    std::vector<int16_t> writebuffer(data.sdr.get_buffer_size() * 2);
    Flags apply = Flags::APPLY_BANDWIDTH | Flags::APPLY_FREQUENCY | Flags::APPLY_GAIN | Flags::APPLY_SAMPLE_RATE;
    while (!has_flag(sdr.get_flags(), Flags::IS_ACTIVE))
    {
        if (data.stop.load())
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
            logs::sdr.warn("ERR read {}", ret_rx);
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

int run_dsp_gui_bridge(SharedData &data, socketData &socket)
{
    IPC server;
    bool init = false;
    std::vector<std::complex<float>> raw;
    std::vector<std::complex<float>> symbols;

    while (!init && !data.stop.load())
    {
        if (!server.start_server(socket.phy_socket))
        {
            logs::socket.error("Failed to start PHY to GUI bridge");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        else
        {
            logs::socket.info("PHY to GUI bridge server started");
            init = true;
        }
    }

    logs::dsp.info("Socket created successfully {}", socket.phy_socket);

    while (!data.stop.load())
    {
        if (data.dsp_sockets_raw.read(raw) == 0)
            server.send_frame(MsgType::Spectrum, raw);
        if (data.dsp_sockets_symbols.read(symbols) == 0)
            server.send_frame(MsgType::Vector, symbols);
    }

    return 0;
};

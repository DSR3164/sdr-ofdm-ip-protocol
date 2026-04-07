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
        if (!data.stop.load())
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
            ret_tx = sdr.writestream(writebuffer);

        if (ret_rx > 0)
            data.sdr_dsp_rx.swap();
        else if (ret_rx == SOAPY_SDR_OVERFLOW)
            logs::sdr.error("OVERFLOW");
        else
            logs::sdr.warn("ERR read {}", ret_rx);
        if (ret_tx < sdr.get_buffer_size())
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

int run_dsp_gui_bridge(SharedData &data, socketData &socket)
{
    IPC server;
    bool socket_init = false;
    std::vector<std::complex<float>> temp;

    server.start_server(socket.phy_socket);
    logs::dsp.info("Socket created succsesfully {}", socket.phy_socket);

    while (!data.stop.load())
    {
        if (!socket_init)
        {
            socket_init = true;
            logs::dsp.info("Client connected succsesfully {}", socket.phy_socket);
        }

        if (data.dsp_sockets.read(temp) == 0)
        {
            server.send_frame(MsgType::Vector, temp);
            logs::dsp.info("Sent frame to GUI, size: {}", temp.size());
        }
    }

    return 0;
};

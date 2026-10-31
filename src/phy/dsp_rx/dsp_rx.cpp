#include "logger.hpp"
#include "phy/dsp_common.hpp"
#include "phy/dsp_rx/demodulator.hpp"
#include "phy/dsp_rx/dsp_rx.hpp"
#include "phy/dsp_rx/equalizer.hpp"
#include "phy/dsp_rx/sync.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <spdlog/fmt/bundled/color.h>
#include <spdlog/spdlog.h>
#include <thread>
#include <vector>

int run_dsp_rx(SharedData &data)
{
    auto &dsp = data.dsp;
    dsp.sample_rate = data.sdr.get_sample_rate();
    auto buff_size = data.sdr.get_buffer_size();
    DSPFrameHeader header;
    DSP::OFDMConfig rx_config;
    rx_config.mod = Modulation::QPSK;

    CarrierGrid grid(dsp.ofdm_cfg);
    FFTWPlan fft(dsp.ofdm_cfg.n_subcarriers, true);

    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    auto clock = std::chrono::steady_clock::now();
    std::vector<uint8_t> bits(4000);

    std::chrono::nanoseconds duration{};

    const int N = dsp.ofdm_cfg.n_subcarriers;
    const int CP = dsp.ofdm_cfg.n_cp;
    const int boundary = buff_size + CP;

    std::vector<int16_t> temp_a(buff_size * 2, 0);
    std::vector<int16_t> temp_b(buff_size * 2, 0);

    std::vector<std::complex<float>> raw_a(buff_size);
    std::vector<std::complex<float>> raw_b(buff_size);
    std::vector<std::complex<float>> header_symbols_raw(N);
    std::vector<std::complex<float>> header_symbols_equalized(N);
    std::vector<uint8_t> header_bits;

    std::vector<std::complex<float>> for_processing;
    std::vector<std::complex<float>> equalized;
    equalized.reserve(buff_size * 2);
    for_processing.reserve(buff_size * 2);

    std::vector<std::complex<float>> zadoff_chu = ofdm_zadoff_chu_symbol(grid);
    std::vector<float> llr;
    const int zc_len = static_cast<int>(zadoff_chu.size());
    size_t data_count = 0;
    size_t symbols_count = 0;
    size_t bits_per_symbol = 1;

    std::vector<std::complex<float>> channel_est;

    ZCCorrFFT zc_corr_fft(buff_size * 2, zadoff_chu);

    auto convert = [](const std::vector<int16_t> &src, std::vector<std::complex<float>> &dst)
    {
        size_t n = src.size() / 2;
        dst.resize(n);
        const int16_t *p = src.data();
        for (size_t i = 0; i < n; ++i, p += 2)
            dst[i] = { static_cast<float>(p[0]), static_cast<float>(p[1]) };
    };

    while (!has_flag(data.sdr.get_flags(), Flags::IS_ACTIVE))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    while (!data.stop.load())
    {
        data.sdr_dsp_rx.read(temp_b, true);
        convert(temp_b, raw_b);

        for_processing.clear();
        for_processing.insert(for_processing.end(), raw_a.begin(), raw_a.end());
        for_processing.insert(for_processing.end(), raw_b.begin(), raw_b.end());
        data.dsp_sockets_raw.write(for_processing);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        start = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        int zc_idx = zc_corr_fft.find_peak(for_processing, 0.3f);

        data.snap.cp_found = false;
        data.snap.zc_found = (zc_idx > 0);
        data.snap.cp_pos = -1;
        data.snap.zc_pos = zc_idx;

        const size_t zc_end = zc_idx + zc_len;
        const size_t needed_after_zc = 10 * (N + CP);
        const size_t total_len = for_processing.size();

        if (zc_idx < 0 || zc_idx >= boundary || zc_end + needed_after_zc > total_len)
        {
            raw_a = std::move(raw_b);
            raw_b.resize(buff_size);
            continue;
        }

        dsp.max_index = zc_idx;

        double epsilon = cfo_est(for_processing, zc_idx, 5, grid, 0.0);
        data.snap.cfo = epsilon * data.sdr.get_sample_rate() / tau;

        if (std::abs(epsilon) > 1e-4)
        {
            float phase_step = -static_cast<float>(epsilon);
            std::complex<float> rot(std::cos(phase_step), std::sin(phase_step));
            std::complex<float> cur_rot(1.f, 0.f);

            for (size_t n = 0; n < for_processing.size(); ++n)
            {
                for_processing[n] *= cur_rot;
                cur_rot *= rot;
            }
        }

        int next = zc_idx + zc_len + dsp.offset;
        next += CP;

        for (size_t i = 0; i < static_cast<size_t>(N); ++i)
        {
            fft.in[i][0] = std::real(for_processing[next + i]);
            fft.in[i][1] = std::imag(for_processing[next + i]);
        }
        fftwf_execute(fft.plan);
        for (size_t i = 0; i < static_cast<size_t>(N); ++i)
            header_symbols_raw[i] = std::complex<float>(fft.out[i][0], fft.out[i][1]);

        ofdm_equalize(header_symbols_raw, header_symbols_equalized, grid, Modulation::QPSK, channel_est);
        demodulate(Modulation::QPSK, header_symbols_equalized, header_bits, llr);

        decode(llr, encoded_header_length(), header_bits);
        unpackFrameHeader(header_bits, grid, header);

        rx_config.mod = header.modulation;
        data_count = header.bits_count;
        bits_per_symbol = get_bits_per_symbol(rx_config.mod);
        symbols_count = (data_count + bits_per_symbol - 1) / bits_per_symbol;

        next += N;

        equalized.clear();

        for (size_t s = 0; s < header.ofdm_symbols_count; ++s)
        {
            if (static_cast<int>(for_processing.size()) - next < N + CP)
                break;

            next += CP;

            for (size_t i = 0; i < static_cast<size_t>(N); ++i)
            {
                fft.in[i][0] = std::real(for_processing[next + i]);
                fft.in[i][1] = std::imag(for_processing[next + i]);
            }
            fftwf_execute(fft.plan);

            std::vector<std::complex<float>> current_symbol(N);
            for (size_t i = 0; i < static_cast<size_t>(N); ++i)
                current_symbol[i] = std::complex<float>(fft.out[i][0], fft.out[i][1]);

            std::vector<std::complex<float>> current_equalized;
            ofdm_equalize(current_symbol, current_equalized, grid, rx_config.mod, channel_est);

            equalized.insert(equalized.end(), current_equalized.begin(), current_equalized.end());

            next += N;
        }

        if (symbols_count > 0 && equalized.size() > symbols_count)
            equalized.resize(symbols_count);

        data.dsp_sockets_symbols.write(equalized);
        demodulate(rx_config.mod, equalized, bits, llr);
        descramble_llr(llr);

        if (data_count > 0)
            llr.resize(data_count);

        data.phy_ip.write(llr, true);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        end = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if ((std::chrono::steady_clock::now().time_since_epoch().count() - clock.time_since_epoch().count()) > 1e9)
        {
            clock = std::chrono::steady_clock::now();
            StatsSnapshot log;
            data.history.get_last(log);
            auto hist = data.history.get_summary();

            logs::dsp.debug(
                "Stats: ZC_idx: {} CFO: {:.0f} ZC_!F: {} TIME: {:.2f}us",
                data.snap.zc_pos, data.snap.cfo,
                hist.zc_not_found, hist.mean_time_us
            );
            logs::tun.debug(
                "{}: {:.4f}%, packets found: {}, packets lost: {}", fmt::format(fg(fmt::color::red), "PL"),
                hist.packet_loss, hist.packet_found, hist.packet_lost
            );
        }
        data.snap.processing_time_us = duration.count() / 1e3;
        raw_a = std::move(raw_b);
        raw_b.resize(buff_size);
    }
    logs::dsp.info("Closing DSP thread");
    return 0;
}

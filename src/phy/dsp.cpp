#include "common.hpp"
#include "phy/dsp.hpp"

int ofdm_zc_corr(const std::vector<std::complex<float>> &r, const std::vector<std::complex<float>> &zc, std::vector<float> &plato)
{
    const int N = zc.size();
    const int L = r.size();

    int best_pos = -1;
    float best_val = 0;

    const float *rptr = reinterpret_cast<const float *>(r.data());
    const float *zptr = reinterpret_cast<const float *>(zc.data());

    float zc_energy = 0;
    for (int n = 0; n < N; n++)
    {
        float br = zptr[2 * n];
        float bi = zptr[2 * n + 1];
        zc_energy += br * br + bi * bi;
    }

    for (int k = 0; k <= L - N; k++)
    {
        float re = 0, im = 0;
        float energy = 0;

        const float *rp = rptr + 2 * k;

        for (int n = 0; n < N; n++)
        {
            float ar = rp[2 * n];
            float ai = rp[2 * n + 1];

            float br = zptr[2 * n];
            float bi = zptr[2 * n + 1];

            re += ar * br + ai * bi;
            im += ai * br - ar * bi;

            energy += ar * ar + ai * ai;
        }

        float corr = re * re + im * im;
        float v = corr / (energy * zc_energy + 1e-12f);

        plato[k] = v;

        if (v > best_val)
        {
            best_val = v;
            best_pos = k;
        }
    }

    return best_pos;
}

void calculate_pilots_and_guard(DSP::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<int> &data, std::vector<bool> &is_pilot, std::vector<bool> &is_guard)
{
    size_t N = static_cast<size_t>(ofdm_config.n_subcarriers);
    int PS = ofdm_config.pilot_spacing;

    pilots.clear();
    is_pilot.resize(N, false);
    is_guard.resize(N, false);

    int counter = 0;
    for (size_t k = 0; k < N; ++k)
    {
        if ((k > N / 2 - 28 and k < N / 2 + 27) or k == 0)
        {
            is_guard[k] = true;
            continue;
        }
        if (counter % PS == 0)
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        else
            data.push_back(k);
        counter++;
    }
};

void calculate_pilots_and_guard(DSP::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<bool> &is_pilot, std::vector<bool> &is_guard)
{
    size_t N = static_cast<size_t>(ofdm_config.n_subcarriers);
    int PS = ofdm_config.pilot_spacing;

    pilots.clear();
    is_pilot.resize(N, false);
    is_guard.resize(N, false);

    int counter = 0;
    for (size_t k = 0; k < N; ++k)
    {
        if ((k > N / 2 - 28 and k < N / 2 + 27) or k == 0)
        {
            is_guard[k] = true;
            continue;
        }
        if (counter % PS == 0)
        {
            pilots.push_back(k);
            is_pilot[k] = true;
        }
        counter++;
    }
};

void ofdm_equalize(std::vector<std::complex<float>> &input, std::vector<std::complex<float>> &output, DSP::OFDMConfig ofdm_config)
{
    int N = ofdm_config.n_subcarriers;
    const std::complex<float> known_pilot = { 1.0f, 0.0f };
    std::vector<std::complex<float>> temp = input;
    output.clear();

    std::vector<int> pilots;
    std::vector<bool> is_pilot(N, false);
    std::vector<bool> is_guard(N, false);

    calculate_pilots_and_guard(ofdm_config, pilots, is_pilot, is_guard);

    std::vector<std::complex<float>> H_prev(N, { 1,0 });

    for (size_t i = 0; i + N <= temp.size(); i += N)
    {
        std::vector<std::complex<float>> sym(temp.begin() + i,
            temp.begin() + i + N);

        std::vector<std::complex<float>> H(N, { 0,0 });
        std::vector<std::complex<float>> equalized(N);

        for (auto k : pilots)
            H[k] = sym[k] / known_pilot;

        for (size_t p = 0; p < pilots.size() - 1; ++p)
        {
            int k1 = pilots[p];
            int k2 = pilots[p + 1];

            auto H1 = H[k1];
            auto H2 = H[k2];

            float a1 = std::arg(H1);
            float a2 = std::arg(H2);

            float da = a2 - a1;
            if (da > M_PIf) da -= 2 * M_PIf;
            if (da < -M_PIf) da += 2 * M_PIf;

            float m1 = std::abs(H1);
            float m2 = std::abs(H2);

            for (int k = k1 + 1; k < k2; ++k)
            {
                if (is_guard[k]) continue;

                float alpha = float(k - k1) / float(k2 - k1);

                float a = a1 + alpha * da;
                float m = m1 + alpha * (m2 - m1);

                H[k] = std::polar(m, a);
            }
        }

        for (int k = 0; k < pilots.front(); ++k)
            if (!is_guard[k]) H[k] = H[pilots.front()];

        for (int k = pilots.back() + 1; k < N; ++k)
            if (!is_guard[k]) H[k] = H[pilots.back()];

        for (int k = 1; k < N; ++k)
            if (std::abs(H[k]) > 1e-12f)
                equalized[k] = sym[k] / H[k];
            else
                equalized[k] = sym[k];

        float phase = 0;
        for (auto k : pilots)
            phase += std::arg(equalized[k] / known_pilot);

        phase /= pilots.size();

        std::complex<float> rot = std::exp(std::complex<float>(0, -phase));

        for (int k = 0; k < N; ++k)
            if (!is_guard[k])
                equalized[k] *= rot;

        for (int k = 0; k < N; ++k)
            if (!is_pilot[k] and !is_guard[k])
                output.push_back(equalized[k]);
    }

}

std::vector<std::complex<float>> generate_zc(int L, int q)
{
    std::vector<std::complex<float>> zc(L);

    for (int n = 0; n < L; ++n)
    {
        float phase = -M_PIf * q * n * (n + 1) / L;
        zc[n] = std::exp(std::complex<float>(0, phase));
    }

    return zc;
}

std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(DSP &data)
{
    FFTWPlan ifft(data.ofdm_cfg.n_subcarriers, false);
    std::vector<std::complex<float>> zadoff_chu;
    auto zc = generate_zc(127, 5);
    zadoff_chu.reserve(data.ofdm_cfg.n_subcarriers);
    ifft.in[0][0] = 0;
    ifft.in[0][1] = 0;

    for (size_t i = 1; i <= 63; ++i)
    {
        ifft.in[i][0] = zc[i - 1].real();
        ifft.in[i][1] = zc[i - 1].imag();
    }

    for (size_t i = 64; i <= 127; ++i)
    {
        ifft.in[i][0] = zc[i - 1].real();
        ifft.in[i][1] = zc[i - 1].imag();
    }

    fftwf_execute(ifft.plan);

    for (int n = 0; n < data.ofdm_cfg.n_subcarriers; ++n)
    {
        ifft.out[n][0] /= (float)(data.ofdm_cfg.n_subcarriers / (3.0 * 16000.0));
        ifft.out[n][1] /= (float)(data.ofdm_cfg.n_subcarriers / (3.0 * 16000.0));
    }

    for (int n = 0; n < data.ofdm_cfg.n_subcarriers; ++n)
        zadoff_chu.push_back(std::complex<float>(ifft.out[n][0], ifft.out[n][1]));

    return zadoff_chu;
};


std::vector<std::complex<float>> cfo_est(const std::vector<std::complex<float>> &signal, DSP data)
{
    int N = data.ofdm_cfg.n_subcarriers;
    int CP = data.ofdm_cfg.n_cp;
    float fs = static_cast<float>(data.sample_rate);
    int start = data.max_index + N;
    std::vector<std::complex<float>> corrected = signal;

    int symbol_len = N + CP;
    for (size_t i = 0; i < 10; ++i)
    {
        int sym_start = start + i * symbol_len;
        if (signal.size() < sym_start + N + CP)
            break;

        std::complex<float> corr = 0;
        for (int n = 0; n < CP; ++n)
            corr += std::conj(signal[sym_start + n]) * signal[sym_start + n + N];

        float epsilon = std::arg(corr) / (2 * M_PIf);
        float delta_f = epsilon * fs / N;

        data.cfo = delta_f;

        for (int n = 0; n < N + CP; ++n)
        {
            float phase = -2 * M_PIf * delta_f * (sym_start + n) / fs;
            corrected[sym_start + n] *= std::complex<float>(std::cos(phase), std::sin(phase));
        }
    }

    return corrected;
}

int run_dsp(SharedData &data)
{
    DSP dsp;
    dsp.sample_rate = data.sdr.get_sample_rate();
    auto buff_size = data.sdr.get_buffer_size();

    FFTWPlan fft(dsp.ofdm_cfg.n_subcarriers, true);
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::vector<std::complex<float>> raw(buff_size);
    std::vector<float> plato(1920);

    std::vector<std::complex<float>> for_processing(buff_size);
    std::vector<std::complex<float>> processed(1920);
    std::vector<std::complex<float>> equalized(1920);
    std::vector<int16_t> temp(buff_size * 2, 0);
    for_processing.reserve(buff_size * 2);
    std::vector<std::complex<float>> zadoff_chu = ofdm_zadoff_chu_symbol(dsp);

    while (!has_flag(data.sdr.get_flags(), Flags::EXIT))
    {
        if (data.sdr_dsp_rx.read(temp) == 0)
        {
            size_t n = temp.size() / 2;
            raw.resize(n);

            int16_t *in = temp.data();
            std::complex<float> *out = raw.data();

            for (size_t i = 0; i < n; ++i)
            {
                float I = in[0];
                float Q = in[1];

                out[i] = { I, Q };
                in += 2;
            }
        }
        else
            continue;

        data.dsp_sockets.write(temp);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        start = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        int next = 0;
        for_processing = raw;
        plato.resize(for_processing.size());
        dsp.max_index = ofdm_zc_corr(for_processing, zadoff_chu, plato);
        for_processing = cfo_est(for_processing, dsp);

        if (static_cast<int>(for_processing.size()) > dsp.max_index + dsp.ofdm_cfg.n_subcarriers * 2)
            next += dsp.max_index + dsp.ofdm_cfg.n_subcarriers;
        for (size_t n = 0; n < 10; ++n)
        {
            if (static_cast<int>(for_processing.size()) - next < dsp.ofdm_cfg.n_subcarriers + dsp.ofdm_cfg.n_cp)
                break;
            next += dsp.ofdm_cfg.n_cp;
            for (size_t i = 0; i < static_cast<size_t>(dsp.ofdm_cfg.n_subcarriers); ++i)
            {
                fft.in[i][0] = std::real(for_processing[next + i]);
                fft.in[i][1] = std::imag(for_processing[next + i]);
            }
            fftwf_execute(fft.plan);

            for (size_t i = 0; i < static_cast<size_t>(dsp.ofdm_cfg.n_subcarriers); ++i)
                processed[i + n * static_cast<size_t>(dsp.ofdm_cfg.n_subcarriers)] = std::complex<float>(fft.out[i][0], fft.out[i][1]);

            next += dsp.ofdm_cfg.n_subcarriers;
        }
        ofdm_equalize(processed, equalized, dsp.ofdm_cfg);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        end = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
    }
    std::cout << "Closing DSP thread\n";
    return 0;
}

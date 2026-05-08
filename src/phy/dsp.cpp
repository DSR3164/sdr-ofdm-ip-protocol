#include "logger.hpp"
#include "phy/dsp.hpp"

#include <cstddef>
#include <fftw3.h>
#include <spdlog/fmt/bundled/color.h>
#include <spdlog/spdlog.h>

extern StatsHistory<60'000> history;
extern StatsSnapshot snap;

namespace
{
    struct FFTWPlan {
        std::vector<float> window;
        fftwf_complex *in = nullptr;
        fftwf_complex *out = nullptr;
        fftwf_plan plan = nullptr;

        FFTWPlan(int size, bool direction = true)
            : window(size)
        {
            for (int i = 0; i < size; ++i)
                window[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(size - 1));

            in = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
            out = reinterpret_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
            if (!in || !out)
                throw std::bad_alloc{};

            plan = fftwf_plan_dft_1d(size, in, out, direction ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_MEASURE);
            if (!plan)
                throw std::runtime_error("fftwf_plan_dft_1d failed");
        }

        ~FFTWPlan()
        {
            if (plan)
                fftwf_destroy_plan(plan);
            if (in)
                fftwf_free(in);
            if (out)
                fftwf_free(out);
        }

        // move constructor
        FFTWPlan(FFTWPlan &&other) noexcept
            : window(std::move(other.window)),
              in(other.in),
              out(other.out),
              plan(other.plan)
        {
            other.in = nullptr;
            other.out = nullptr;
            other.plan = nullptr;
        }

        FFTWPlan &operator=(FFTWPlan &&other) noexcept
        {
            if (this != &other)
            {
                if (plan)
                    fftwf_destroy_plan(plan);
                if (in)
                    fftwf_free(in);
                if (out)
                    fftwf_free(out);

                window = std::move(other.window);
                in = other.in;
                out = other.out;
                plan = other.plan;

                other.in = nullptr;
                other.out = nullptr;
                other.plan = nullptr;
            }
            return *this;
        }
        FFTWPlan(const FFTWPlan &) = delete;
        FFTWPlan &operator=(const FFTWPlan &) = delete;
    };

    void bpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             bits[i] * -2.0 + 1.0,
                             bits[i] * -2.0 + 1.0
                         )
                         / sqrtf(2);
    }

    void qpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             bits[2 * i + 0] * -2.0 + 1.0,
                             bits[2 * i + 1] * -2.0 + 1.0
                         )
                         / sqrtf(2.0);
    }

    void qam16_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             (1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
                             (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3]))
                         )
                         / sqrtf(10.0);
    }

    void qam64_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             (1 - 2 * bits[6 * i + 0]) * (4 - (1 - 2 * bits[6 * i + 2]) * (2 - (1 - 2 * bits[6 * i + 4]))),
                             (1 - 2 * bits[6 * i + 1]) * (4 - (1 - 2 * bits[6 * i + 3]) * (2 - (1 - 2 * bits[6 * i + 5])))
                         )
                         / sqrtf(42.0);
    }

    static std::pair<uint8_t, uint8_t> demap_component_3gpp(float val)
    {
        uint8_t b_sign = (val < 0.0f) ? 1 : 0;
        uint8_t b_amp = (std::abs(val) < 2.0f) ? 0 : 1;
        return { b_sign, b_amp };
    }

    void demodulate(Modulation mod, const std::vector<std::complex<float>> &symbols, std::vector<uint8_t> &bits)
    {

        switch (mod)
        {
        case Modulation::BPSK: {
            bits.resize(symbols.size());

            for (size_t i = 0; i < symbols.size(); ++i)
                bits[i] = demap_component_3gpp(symbols[i].real() + symbols[i].imag()).first;
            break;
        }
        case Modulation::QPSK: {
            bits.resize(symbols.size() * 2);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                bits[2 * i + 0] = demap_component_3gpp(symbols[i].real()).first;
                bits[2 * i + 1] = demap_component_3gpp(symbols[i].imag()).first;
            }
            break;
        }
        case Modulation::QAM16: {
            bits.resize(symbols.size() * 4);
            const float scale = std::sqrt(10.0f);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                auto [b0, b2] = demap_component_3gpp(symbols[i].real() * scale);
                auto [b1, b3] = demap_component_3gpp(symbols[i].imag() * scale);

                bits[4 * i + 0] = b0;
                bits[4 * i + 1] = b1;
                bits[4 * i + 2] = b2;
                bits[4 * i + 3] = b3;
            }
            break;
        }
        case Modulation::QAM64: {
            bits.resize(symbols.size() * 6);

            std::vector<std::complex<float>> s(1);
            std::vector<uint8_t> b(6);

            struct Point {
                float I, Q;
                uint8_t bits[6];
            };
            std::vector<Point> constellation(64);

            for (int idx = 0; idx < 64; ++idx)
            {
                for (int bit = 0; bit < 6; ++bit)
                    b[bit] = (idx >> (5 - bit)) & 1;
                qam64_mapper_3gpp(b, s);
                constellation[idx].I = s[0].real();
                constellation[idx].Q = s[0].imag();
                for (int bit = 0; bit < 6; ++bit)
                    constellation[idx].bits[bit] = b[bit];
            }

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                float rI = symbols[i].real();
                float rQ = symbols[i].imag();

                int best_idx = 0;
                float best_dist = std::numeric_limits<float>::max();

                for (int j = 0; j < 64; ++j)
                {
                    float dI = rI - constellation[j].I;
                    float dQ = rQ - constellation[j].Q;
                    float dist = dI * dI + dQ * dQ;
                    if (dist < best_dist)
                    {
                        best_dist = dist;
                        best_idx = j;
                    }
                }

                for (int bit = 0; bit < 6; ++bit)
                    bits[6 * i + bit] = constellation[best_idx].bits[bit];
            }
            break;
        }
        default:
            logs::dsp.warn("Неподдерживаемый тип демодуляции");
        }
    }

    void split_to_float(const std::complex<float> *__restrict src, float *__restrict dst_re, float *__restrict dst_im, size_t n)
    {
        const float *raw_src = reinterpret_cast<const float *>(src);

        for (size_t i = 0; i < n; ++i)
        {
            dst_re[i] = raw_src[2 * i];
            dst_im[i] = raw_src[2 * i + 1];
        }
    }

    int zc_sync(const std::vector<std::complex<float>> &for_ofdm, const std::vector<std::complex<float>> &zadoff_chu, const float zc_energy, float threshold, int cp_index, int Lcp)
    {
        auto N = zadoff_chu.size();
        auto L = for_ofdm.size();
        int sym_len = N + Lcp;

        static std::vector<float> r_re(L);
        static std::vector<float> r_im(L);
        static std::vector<float> zc_re(N);
        static std::vector<float> zc_im(N);

        split_to_float(for_ofdm.data(), r_re.data(), r_im.data(), L);
        split_to_float(zadoff_chu.data(), zc_re.data(), zc_im.data(), N);

        float max_norm = -1.f;
        int best_idx = -1;

        for (int k = 1; k * sym_len <= cp_index; ++k)
        {
            int center = cp_index - k * sym_len;

            for (int offset = -1.1 * Lcp; offset <= 1.1 * Lcp; ++offset)
            {
                int n = center + offset;
                if (n < 0 || n + (int)N > (int)L)
                    continue;

                float sum_re = 0.0f;
                float sum_im = 0.0f;
                float sig_energy = 0.0f;

                for (size_t i = 0; i < N; ++i)
                {
                    float sr = r_re[n + i];
                    float si = r_im[n + i];
                    float zr = zc_re[i];
                    float zi = zc_im[i];

                    sum_re += sr * zr + si * zi;
                    sum_im += si * zr - sr * zi;
                    sig_energy += sr * sr + si * si;
                }

                float corr = sum_re * sum_re + sum_im * sum_im;
                float norm = corr / (sig_energy * zc_energy + 1e-12f);

                if (norm > max_norm && norm > threshold)
                {
                    max_norm = norm;
                    best_idx = n;
                }
                else if (best_idx > 0 && norm < threshold)
                    return best_idx;
            }
        }

        return best_idx;
    }

    int ofdm_cp_corr(const std::vector<std::complex<float>> &r, int N, int Lcp, std::vector<float> &plato)
    {
        int size = r.size();
        float max_metric = 0.0f;
        int max_index = -1;

        std::complex<float> P = 0.0f;
        float R = 0.0f;
        float R_cp = 0.0f;

        for (int i = 0; i < Lcp; i++)
        {
            P += r[i] * std::conj(r[i + N]);
            R += std::norm(r[i + N]);
            R_cp += std::norm(r[i]);
        }
        for (int d = 0; d < size - N - Lcp; d++)
        {

            float denom = 0.5f * (R_cp + R);
            float metric = std::norm(P) / (denom * denom + 1e-12f);

            if (metric > max_metric and metric > 0.85)
            {
                max_metric = metric;
                max_index = d;
            }
            else if (max_index != -1 && metric < max_metric * 0.7f)
                break;

            if (d + 1 >= size - N - Lcp)
                break;

            P -= r[d] * std::conj(r[d + N]);
            P += r[d + Lcp] * std::conj(r[d + N + Lcp]);

            R -= std::norm(r[d + N]);
            R += std::norm(r[d + N + Lcp]);

            R_cp -= std::norm(r[d]);
            R_cp += std::norm(r[d + Lcp]);
            plato[d] = metric;
        }

        return max_index;
    }

    void calculate_pilots_and_guard(DSP::OFDMConfig ofdm_config, std::vector<int> &pilots, std::vector<int> &data, std::vector<bool> &is_pilot, std::vector<bool> &is_guard)
    {
        size_t N = static_cast<size_t>(ofdm_config.n_subcarriers);
        int PS = ofdm_config.pilot_spacing;

        data.clear();
        pilots.clear();
        is_pilot.resize(N, false);
        is_guard.resize(N, false);

        int counter = 0;
        for (size_t k = 0; k < N; ++k)
        {
            if (k == 0 || (k >= 37 && k <= 91))
            {
                is_guard[k] = true;
                continue;
            }
            if ((counter % PS == 0) || (k == N / 2 - 28) || (k == N / 2 + 28) || (k == N - 1))
            {
                pilots.push_back(k);
                is_pilot[k] = true;
            }
            else
                data.push_back(k);
            counter++;
        }
    };

    void ofdm_equalize(std::vector<std::complex<float>> &input, std::vector<std::complex<float>> &output, DSP::OFDMConfig &ofdm_config)
    {
        int N = ofdm_config.n_subcarriers;
        float accumulated_phase = 0;
        const std::complex<float> known_pilot = { 1.0f, 0.0f };
        std::vector<std::complex<float>> temp = input;
        output.clear();

        std::vector<int> pilots;
        std::vector<int> data;
        std::vector<bool> is_pilot(N, false);
        std::vector<bool> is_guard(N, false);

        calculate_pilots_and_guard(ofdm_config, pilots, data, is_pilot, is_guard);

        std::vector<std::complex<float>> H_prev(N, { 1, 0 });

        for (size_t i = 0; i + N <= temp.size(); i += N)
        {
            std::vector<std::complex<float>> sym(temp.begin() + i, temp.begin() + i + N);

            std::vector<std::complex<float>> H(N, { 0, 0 });
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
                if (da > M_PIf)
                    da -= 2 * M_PIf;
                if (da < -M_PIf)
                    da += 2 * M_PIf;

                float m1 = std::abs(H1);
                float m2 = std::abs(H2);

                for (int k = k1 + 1; k < k2; ++k)
                {
                    if (is_guard[k])
                        continue;

                    float alpha = float(k - k1) / float(k2 - k1);

                    float a = a1 + alpha * da;
                    float m = m1 + alpha * (m2 - m1);

                    H[k] = std::polar(m, a);
                }
            }

            for (int k = 0; k < pilots.front(); ++k)
                if (!is_guard[k])
                    H[k] = H[pilots.front()];

            for (int k = pilots.back() + 1; k < N; ++k)
                if (!is_guard[k])
                    H[k] = H[pilots.back()];

            for (int k = 1; k < N; ++k)
                if (std::abs(H[k]) > 1e-12f)
                    equalized[k] = sym[k] / H[k];
                else
                    equalized[k] = sym[k];

            float cpe = 0;
            for (auto k : pilots)
                cpe += std::arg(equalized[k] / known_pilot);
            cpe /= pilots.size();

            accumulated_phase += cpe;

            float mean_amp_pilots = 0;
            for (auto k : pilots)
                mean_amp_pilots += std::abs(equalized[k]);
            mean_amp_pilots /= pilots.size();

            for (int k = 0; k < N; ++k)
                if (!is_guard[k])
                    equalized[k] /= mean_amp_pilots;

            std::complex<float> rot = std::exp(std::complex<float>(0, -accumulated_phase));
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
        static FFTWPlan ifft(data.ofdm_cfg.n_subcarriers, false);
        std::vector<std::complex<float>> zadoff_chu;
        auto zc = generate_zc(127, 5);
        zadoff_chu.reserve(data.ofdm_cfg.n_subcarriers);
        ifft.in[0][0] = 0;
        ifft.in[0][1] = 0;

        for (size_t i = 1; i <= 127; ++i)
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

    float coarse_cfo(std::vector<std::complex<float>> &r, int max_index, int N, int Lcp, float fs)
    {
        std::complex<float> P = 0.0f;
        for (int i = 0; i < Lcp; ++i)
            P += r[max_index + i] * std::conj(r[max_index + i + N]);

        float epsilon = std::arg(P) / (2 * M_PIf);

        float cfo_hz = epsilon * fs / N;

        for (size_t n = 0; n < r.size(); ++n)
        {
            float phase = 2 * M_PIf * cfo_hz * n / fs;
            r[n] *= std::complex<float>(std::cos(phase), std::sin(phase));
        }

        return cfo_hz;
    }

    void cfo_est(std::vector<std::complex<float>> &signal, DSP &data)
    {
        int N = data.ofdm_cfg.n_subcarriers;
        int CP = data.ofdm_cfg.n_cp;
        float fs = static_cast<float>(data.sample_rate);
        int start = data.max_index + N;

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
                signal[sym_start + n] *= std::complex<float>(std::cos(phase), std::sin(phase));
            }
        }
    }

    void ofdm(std::vector<uint8_t> &bits, std::vector<int16_t> &buffer, DSP &dsp_config)
    {
        auto &ofdm_config = dsp_config.ofdm_cfg;
        int Ncp = ofdm_config.n_cp;
        int N = ofdm_config.n_subcarriers;
        int pilot_spacing = ofdm_config.pilot_spacing;
        Modulation modulation_type = ofdm_config.mod;

        if (N < 4 or pilot_spacing < 2)
            return;

        buffer.clear();
        std::vector<std::complex<float>> symbols(bits.size());
        std::vector<std::complex<float>> schmidl(N);
        auto zc = generate_zc(127, 5);
        switch (modulation_type)
        {
        case Modulation::BPSK: {
            bpsk_mapper_3gpp(bits, symbols);
            break;
        }
        case Modulation::QPSK: {
            if (bits.size() % 2 != 0)
                bits.resize(bits.size() + (2 - bits.size() % 2), 0);
            symbols.resize(bits.size() / 2);
            qpsk_mapper_3gpp(bits, symbols);
            break;
        }
        case Modulation::QAM16: {
            if (bits.size() % 4 != 0)
                bits.resize(bits.size() + (4 - bits.size() % 4), 0);
            symbols.resize(bits.size() / 4);
            qam16_mapper_3gpp(bits, symbols);
            break;
        }
        case Modulation::QAM64: {
            if (bits.size() % 6 != 0)
                bits.resize(bits.size() + (6 - bits.size() % 6), 0);
            symbols.resize(bits.size() / 6);
            qam64_mapper_3gpp(bits, symbols);
            break;
        }
        default: {
            if (bits.size() % 4 != 0)
                bits.resize(bits.size() + (4 - bits.size() % 4), 0);
            symbols.resize(bits.size() / 4);
            qpsk_mapper_3gpp(bits, symbols);
            break;
        }
        }

        static FFTWPlan ifft(N, false);

        int total_symbols = (int)symbols.size();
        std::vector<int> data;
        std::vector<int> pilots;
        std::vector<bool> is_guard;
        std::vector<bool> is_pilot;
        calculate_pilots_and_guard(ofdm_config, pilots, data, is_pilot, is_guard);

        int symbols_per_ofdm = static_cast<int>(data.size());
        int num_ofdm_symbols = (total_symbols + symbols_per_ofdm - 1) / symbols_per_ofdm;

        buffer.reserve((num_ofdm_symbols + Ncp) * (N + 2));

        auto ofdm_zc_symbol = ofdm_zadoff_chu_symbol(dsp_config);

        for (size_t i = 0; i < ofdm_zc_symbol.size(); ++i)
        {
            buffer.push_back(static_cast<int16_t>(ofdm_zc_symbol[i].real()));
            buffer.push_back(static_cast<int16_t>(ofdm_zc_symbol[i].imag()));
        }

        for (int sym = 0; sym < num_ofdm_symbols; ++sym)
        {
            for (int i = 0; i < N; ++i)
            {
                ifft.in[i][0] = 0.0f;
                ifft.in[i][1] = 0.0f;
            }

            for (int k : pilots)
            {
                ifft.in[k][0] = 1.0f;
                ifft.in[k][1] = 0.0f;
            }

            for (int i = 0; i < data.size(); ++i)
            {
                int idx = sym * symbols_per_ofdm + i;
                int k = data[i];

                if (idx < total_symbols)
                {
                    ifft.in[k][0] = std::real(symbols[idx]);
                    ifft.in[k][1] = std::imag(symbols[idx]);
                }
                else
                {
                    ifft.in[k][0] = 0.0f;
                    ifft.in[k][1] = 0.0f;
                }
            }

            fftwf_execute(ifft.plan);

            // Norm
            for (int n = 0; n < N; ++n)
            {
                ifft.out[n][0] /= (float)(N / (3.0 * 16000.0f));
                ifft.out[n][1] /= (float)(N / (3.0 * 16000.0f));
            }

            // Cyclic Prefix
            for (int n = N - Ncp; n < N; ++n)
            {
                buffer.push_back((int16_t)ifft.out[n][0]);
                buffer.push_back((int16_t)ifft.out[n][1]);
            }

            // Data
            for (int n = 0; n < N; ++n)
            {
                buffer.push_back((int16_t)ifft.out[n][0]);
                buffer.push_back((int16_t)ifft.out[n][1]);
            }
        }
    }
} // namespace

int run_dsp_rx(SharedData &data)
{
    auto &dsp = data.dsp;
    dsp.sample_rate = data.sdr.get_sample_rate();
    auto buff_size = data.sdr.get_buffer_size();
    float zc_energy = 0.0f;

    FFTWPlan fft(dsp.ofdm_cfg.n_subcarriers, true);
    std::chrono::steady_clock::time_point start;
    std::chrono::steady_clock::time_point end;
    std::chrono::steady_clock::time_point clock;
    std::vector<float> plato(buff_size * 2);
    std::vector<uint8_t> bits(4000);

    float coarse_mean = 0.0f;
    float coarse = 0.0f;
    float alpha = 0.01f;
    int cp_idx = -4;
    std::chrono::nanoseconds duration{};

    const int N = dsp.ofdm_cfg.n_subcarriers;
    const int CP = dsp.ofdm_cfg.n_cp;

    std::vector<int16_t> temp_a(buff_size * 2, 0);
    std::vector<int16_t> temp_b(buff_size * 2, 0);

    std::vector<std::complex<float>> raw_a(buff_size);
    std::vector<std::complex<float>> raw_b(buff_size);

    std::vector<std::complex<float>> for_processing;
    std::vector<std::complex<float>> processed(buff_size * 2);
    std::vector<std::complex<float>> equalized(buff_size * 2);
    for_processing.reserve(buff_size * 2);
    std::vector<std::complex<float>> zadoff_chu = ofdm_zadoff_chu_symbol(dsp);
    const int zc_len = static_cast<int>(zadoff_chu.size());

    const float *zptr = reinterpret_cast<const float *>(zadoff_chu.data());
    for (size_t n = 0; n < zadoff_chu.size() * 2; ++n)
        zc_energy += zptr[n] * zptr[n];

    auto convert = [](const std::vector<int16_t> &src,
                      std::vector<std::complex<float>> &dst)
    {
        size_t n = src.size() / 2;
        dst.resize(n);
        const int16_t *p = src.data();
        for (size_t i = 0; i < n; ++i, p += 2)
            dst[i] = { static_cast<float>(p[0]), static_cast<float>(p[1]) };
    };
    while (!data.stop.load())
    {
        data.sdr_dsp_rx.read(temp_b, true);
        convert(temp_b, raw_b);

        for_processing.clear();
        for_processing.insert(for_processing.end(), raw_a.begin(), raw_a.end());
        for_processing.insert(for_processing.end(), raw_b.begin(), raw_b.end());
        data.dsp_sockets_raw.write(for_processing);

        const int boundary = static_cast<int>(raw_a.size());

        plato.resize(for_processing.size());

        std::atomic_signal_fence(std::memory_order_seq_cst);
        start = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);

        cp_idx = ofdm_cp_corr(for_processing, N, CP, plato);
        coarse = coarse_cfo(for_processing, cp_idx, N, CP, data.sdr.get_sample_rate());
        coarse_mean = alpha * coarse + (1.0f - alpha) * coarse_mean;

        int zc_idx = zc_sync(for_processing, zadoff_chu, zc_energy, 0.3f, cp_idx, CP) + dsp.offset;

        snap.cp_found = cp_idx > 0;
        snap.zc_found = zc_idx > 0;
        snap.cp_pos = cp_idx;
        snap.zc_pos = zc_idx;
        snap.cfo = coarse;

        const int zc_end = zc_idx + zc_len;
        const int needed_after_zc = 10 * (N + CP);
        const int total_len = static_cast<int>(for_processing.size());

        if (zc_idx >= boundary or zc_end + needed_after_zc > total_len or zc_idx < 0)
        {
            raw_a = std::move(raw_b);
            raw_b.resize(buff_size);
            continue;
        }

        dsp.max_index = zc_idx;
        cfo_est(for_processing, dsp);

        int next = 0;
        int last = 0;

        if (static_cast<int>(for_processing.size()) > zc_idx + zc_len + N)
            next = zc_idx + zc_len;
        else
        {
            raw_a = raw_b;
            continue;
        }

        for (size_t s = 0; s < 10; ++s)
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

            for (size_t i = 0; i < static_cast<size_t>(N); ++i)
                processed[i + s * static_cast<size_t>(N)] = std::complex<float>(fft.out[i][0], fft.out[i][1]);

            next += N;
            last = next;
        }
        if (last > 0)
            processed.resize(last);

        ofdm_equalize(processed, equalized, dsp.ofdm_cfg);

        data.dsp_sockets_symbols.write(equalized);
        demodulate(dsp.ofdm_cfg.mod, equalized, bits);
        data.phy_ip.write(bits, true);

        std::atomic_signal_fence(std::memory_order_seq_cst);
        end = std::chrono::steady_clock::now();
        std::atomic_signal_fence(std::memory_order_seq_cst);
        duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

        if ((std::chrono::steady_clock::now().time_since_epoch().count() - clock.time_since_epoch().count()) > 1e9)
        {
            clock = std::chrono::steady_clock::now();
            StatsSnapshot log;
            history.get_last(log);
            auto hist = history.get_summary();
            logs::dsp.debug(
                "{} packet: CP: {}; ZC: {}; CFO: {:.0f}; ZC_F: {}; CP_F: {}, CP_F: {}; ZC_F: {}; CFO_J: {}; TIME: {:.2f}", fmt::format(fg(fmt::color::beige), "Current"),
                snap.cp_pos, snap.zc_pos, snap.cfo, snap.zc_found, snap.cp_found,
                hist.cp_not_found, hist.zc_not_found, hist.cfo_jumped, hist.mean_time_us
            );
            logs::tun.debug(
                "{}: {}%, packets found: {}, packets lost: {}", fmt::format(fg(fmt::color::red), "PL"),
                hist.packet_loss, hist.packet_found, hist.packet_lost
            );
        }
        snap.processing_time_us = duration.count() / 1e3;
        raw_a = std::move(raw_b);
        raw_b.resize(buff_size);
    }
    logs::dsp.info("Closing DSP thread");
    return 0;
}

int run_dsp_tx(SharedData &data)
{
    logs::dsp.info("[{}] Starting", fmt::format(fmt::fg(fmt::color::cyan), "TX"));
    std::vector<uint8_t> bits;
    std::vector<int16_t> buffer;

    while (!data.stop.load())
    {
        data.ip_phy.read(bits, true);

        ofdm(bits, buffer, data.dsp);
        logs::dsp.trace("[{}] modulate {} samples", fmt::format(fmt::fg(fmt::color::cyan), "OFDM"), buffer.size());
        data.sdr_dsp_tx.write(buffer);
    }

    return 0;
}

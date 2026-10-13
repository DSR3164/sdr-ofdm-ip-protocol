#include "logger.hpp"
#include "phy/dsp.hpp"
#include "ip/bit_utils.hpp"
#include "ip/fec_codec.hpp"

#include <cstddef>
#include <cstdint>
#include <fftw3.h>
#include <mutex>
#include <numbers>
#include <numeric>
#include <span>
#include <spdlog/fmt/bundled/color.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace
{
    constexpr auto pi = std::numbers::pi_v<float>;
    constexpr auto tau = std::numbers::pi_v<float> * 2.0f;

    constexpr size_t header_raw_bits = 32;
    constexpr size_t max_data_symbols = 15;
    constexpr size_t max_payload_bits = std::numeric_limits<uint16_t>::max();

    [[nodiscard]] constexpr size_t get_bits_per_symbol(Modulation mod) noexcept
    {
        switch (mod)
        {
        case Modulation::BPSK:
            return 1;
        case Modulation::QPSK:
            return 2;
        case Modulation::QAM16:
            return 4;
        case Modulation::QAM64:
            return 6;
        }
        return 0;
    }

    [[nodiscard]] constexpr std::complex<float> get_pilot(Modulation mod) noexcept
    {
        switch (mod)
        {
        case Modulation::BPSK:
            [[fallthrough]];
        case Modulation::QPSK:
            return std::complex(0.707106781f, 0.707106781f) / std::sqrt(2.0f);
        case Modulation::QAM16:
            return std::complex(0.948683260f, 0.948683260f) / std::sqrt(2.0f);
        case Modulation::QAM64:
            return std::complex(1.080123500f, 1.080123500f) / std::sqrt(2.0f);
        }
        return 1.0f;
    }

    int16_t clip(float x)
    {
        return static_cast<int16_t>(std::clamp(x, -16384.0f, 16384.0f));
    }

    size_t ceil_div(size_t value, size_t divisor)
    {
        return value / divisor + static_cast<size_t>(value % divisor != 0);
    }

    std::mutex &fftw_mutex()
    {
        static std::mutex mutex;
        return mutex;
    }

    struct FFTWPlan {
        size_t size;
        fftwf_complex *in = nullptr;
        fftwf_complex *out = nullptr;
        fftwf_plan plan = nullptr;

        FFTWPlan(int size, bool direction = true)
            : size(size)
        {
            if (size == 0 || size > static_cast<size_t>(std::numeric_limits<int>::max()))
                throw std::invalid_argument("Invalid FFT length");

            std::scoped_lock lock(fftw_mutex());

            in = static_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));
            out = static_cast<fftwf_complex *>(fftwf_malloc(sizeof(fftwf_complex) * size));

            if (!in || !out)
            {
                fftwf_free(in);
                fftwf_free(out);
                throw std::bad_alloc{};
            }

            plan = fftwf_plan_dft_1d(static_cast<int>(size), in, out, direction ? FFTW_FORWARD : FFTW_BACKWARD, FFTW_ESTIMATE);
            if (!plan)
            {
                fftwf_free(in);
                fftwf_free(out);
                throw std::runtime_error("fftwf_plan_dft_1d failed");
            }
        }

        ~FFTWPlan()
        {
            std::scoped_lock lock(fftw_mutex());
            fftwf_destroy_plan(plan);
            fftwf_free(in);
            fftwf_free(out);
        }

        FFTWPlan(const FFTWPlan &) = delete;
        FFTWPlan &operator=(const FFTWPlan &) = delete;
        FFTWPlan(FFTWPlan &&) = delete;
        FFTWPlan &operator=(FFTWPlan &&) = delete;

        void clear()
        {
            for (size_t i = 0; i < size; ++i)
            {
                in[i][0] = 0.0f;
                in[i][1] = 0.0f;
            }
        }
    };

    struct CarrierGrid {
        size_t n;
        size_t cp;
        size_t spacing;
        size_t half_active;
        std::vector<size_t> pilots;
        std::vector<size_t> data;
        std::vector<bool> is_guard;

        explicit CarrierGrid(const DSP::OFDMConfig &config)
            : n(config.n_subcarriers),
              cp(config.n_cp),
              spacing(config.pilot_spacing),
              half_active(0)
        {
            if (n < 128 || n % 2 != 0 || n > static_cast<size_t>(std::numeric_limits<int>::max()) / 128 || cp == 0 || cp > n || spacing < 2)
                throw std::invalid_argument("Invalid OFDM configuration");

            half_active = n * 9 / 32;
            is_guard.assign(n, true);
            size_t counter = 0;
            for (size_t k = 1; k < n; ++k)
            {
                if (k > half_active && k < n - half_active)
                    continue;

                is_guard[k] = false;
                if (counter % spacing == 0 || k == half_active || k == n - half_active || k == n - 1)
                    pilots.push_back(k);
                else
                    data.push_back(k);
                ++counter;
            }
            if (data.empty() || pilots.size() < 4)
                throw std::invalid_argument("OFDM grid has no usable data carriers");
        }

        size_t symbol_length() const { return n + cp; }
        size_t active_count() const { return 2 * half_active; }
    };

    void scramble(std::vector<uint8_t> &bits, uint8_t seed = 0x5B)
    {
        if (seed == 0 || seed > 0x7F)
        {
            logs::dsp.warn("Scramble seed must be in range [0x01, 0x7F]");
            return;
        }

        uint8_t lfsr = seed;

        for (uint8_t &bit : bits)
        {
            uint8_t feedback = ((lfsr >> 6) ^ (lfsr >> 3)) & 1;
            lfsr = ((lfsr << 1) | feedback) & 0x7F;
            bit ^= feedback;
        }
    }

    void descramble(std::vector<uint8_t> &bits, uint8_t seed = 0x5B)
    {
        scramble(bits, seed);
    }

    void descramble_llr(std::vector<float> &llr_values, uint8_t seed = 0x5B)
    {
        if (seed == 0 || seed > 0x7F)
        {
            logs::dsp.warn("Scramble seed must be in range [0x01, 0x7F]");
            return;
        }

        uint8_t lfsr = seed;

        for (float &llr : llr_values)
        {
            uint8_t feedback = ((lfsr >> 6) ^ (lfsr >> 3)) & 1;
            lfsr = ((lfsr << 1) | feedback) & 0x7F;
            if (feedback)
                llr = -llr;
        }
    }

    struct FrameHeader {
        Modulation modulation = Modulation::QAM64;
        uint8_t ofdm_symbols_count = 0; // Default ofdm symbols count
        uint16_t bits_count = 0;        // Max bits per frame
    };

    std::vector<uint8_t> packFrameHeader(const FrameHeader &header)
    {
        std::vector<uint8_t> bits(header_raw_bits, 0);
        uint8_t mod_val = static_cast<uint8_t>(header.modulation);
        bits[0] = (mod_val >> 1) & 1;
        bits[1] = mod_val & 1;
        for (int i = 0; i < 4; ++i)
            bits[2 + i] = (header.ofdm_symbols_count >> (3 - i)) & 1;
        for (int i = 0; i < 16; ++i)
            bits[6 + i] = (header.bits_count >> (15 - i)) & 1;
        return bits;
    }

    bool unpackFrameHeader(const std::vector<uint8_t> &bits, const CarrierGrid &grid, FrameHeader &header)
    {
        FrameHeader candidate;
        candidate.modulation = static_cast<Modulation>((bits[0] << 1) | bits[1]);
        for (size_t i = 0; i < 4; ++i)
            candidate.ofdm_symbols_count |= static_cast<uint8_t>(bits[2 + i] << (3 - i));
        for (size_t i = 0; i < 16; ++i)
            candidate.bits_count |= static_cast<uint16_t>(bits[6 + i] << (15 - i));

        const size_t bps = get_bits_per_symbol(candidate.modulation);
        if (bps == 0 || candidate.bits_count == 0 || candidate.ofdm_symbols_count == 0 || candidate.ofdm_symbols_count > max_data_symbols)
            return false;
        if (ceil_div(candidate.bits_count, grid.data.size() * bps) != candidate.ofdm_symbols_count)
            return false;

        header = candidate;
        return true;
    }

    std::vector<uint8_t> encode(const std::vector<uint8_t> &data)
    {
        auto bytes = bits_to_bytes<uint8_t>(data, 8);
        auto encoded = conv_encoder(bytes);
        auto bits = byte_to_bits(encoded, 8);
        auto inter = interleaving_(bits);
        return inter;
    }

    size_t encoded_header_length()
    {
        const auto encoded = encode(std::vector<uint8_t>(header_raw_bits, 0));
        if (encoded.empty())
            throw std::runtime_error("Header encoder returned no bits");
        return encoded.size();
    }

    bool decode(std::vector<float> &llr, size_t encoded_size, std::vector<uint8_t> &bits)
    {
        if (llr.size() < encoded_size)
            return false;
        llr.resize(encoded_size);
        descramble_llr(llr);
        auto bytes = viterbi_decoder_llr(deinterleaving_float(llr));
        if (bytes.size() < header_raw_bits / 8)
            return false;
        bytes.resize(header_raw_bits / 8);
        bits = byte_to_bits(bytes, 8);
        return bits.size() == header_raw_bits;
    }

    void bpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             bits[i] * -2.0 + 1.0,
                             bits[i] * -2.0 + 1.0
                         )
                         / std::sqrt(2.0f);
    }

    void qpsk_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             bits[2 * i + 0] * -2.0 + 1.0,
                             bits[2 * i + 1] * -2.0 + 1.0
                         )
                         / std::sqrt(2.0f);
    }

    void qam16_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             (1 - 2 * bits[4 * i + 0]) * (2 - (1 - 2 * bits[4 * i + 2])),
                             (1 - 2 * bits[4 * i + 1]) * (2 - (1 - 2 * bits[4 * i + 3]))
                         )
                         / std::sqrt(10.0f);
    }

    void qam64_mapper_3gpp(const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        for (size_t i = 0; i < symbols.size(); ++i)
            symbols[i] = std::complex<float>(
                             (1 - 2 * bits[6 * i + 0]) * (4 - (1 - 2 * bits[6 * i + 2]) * (2 - (1 - 2 * bits[6 * i + 4]))),
                             (1 - 2 * bits[6 * i + 1]) * (4 - (1 - 2 * bits[6 * i + 3]) * (2 - (1 - 2 * bits[6 * i + 5])))
                         )
                         / std::sqrt(42.0f);
    }

    void demodulate(Modulation mod, const std::vector<std::complex<float>> &symbols, std::vector<uint8_t> &bits, std::vector<float> &llr)
    {
        switch (mod)
        {
        case Modulation::BPSK: {
            llr.resize(symbols.size());
            const float scale = std::sqrt(2.0f);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                const auto sr = symbols[i].real() * scale;
                const auto si = symbols[i].imag() * scale;

                llr[i] = sr + si;
            }
            break;
        }
        case Modulation::QPSK: {
            llr.resize(symbols.size() * 2);
            const float scale = std::sqrt(2.0f);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                const auto sr = symbols[i].real() * scale;
                const auto si = symbols[i].imag() * scale;
                const size_t shift = i * 2;

                llr[shift + 0] = sr;
                llr[shift + 1] = si;
            }
            break;
        }
        case Modulation::QAM16: {
            llr.resize(symbols.size() * 4);
            const float scale = std::sqrt(10.0f);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                const auto sr = symbols[i].real() * scale;
                const auto si = symbols[i].imag() * scale;
                const size_t shift = i * 4;

                llr[shift + 0] = sr;
                llr[shift + 1] = si;

                llr[shift + 2] = 2.0f - std::abs(sr);
                llr[shift + 3] = 2.0f - std::abs(si);
            }
            break;
        }
        case Modulation::QAM64: {
            llr.resize(symbols.size() * 6);

            const float scale = std::sqrt(42.0f);

            for (size_t i = 0; i < symbols.size(); ++i)
            {
                const auto sr = symbols[i].real() * scale;
                const auto si = symbols[i].imag() * scale;
                const auto l1r = std::abs(sr);
                const auto l1i = std::abs(si);
                const size_t shift = i * 6;

                llr[shift + 0] = sr;
                llr[shift + 1] = si;

                llr[shift + 2] = 4.0f - l1r;
                llr[shift + 3] = 4.0f - l1i;

                llr[shift + 4] = 2.0f - std::abs(llr[shift + 2]);
                llr[shift + 5] = 2.0f - std::abs(llr[shift + 3]);
            }
            break;
        }
        default:
            logs::dsp.warn("Неподдерживаемый тип демодуляции");
        }
        bits.resize(llr.size());
        for (size_t i = 0; i < llr.size(); ++i)
            bits[i] = llr[i] < 0.0f ? 1 : 0;
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

    int zc_sync_full(const std::vector<std::complex<float>> &for_ofdm, const std::vector<std::complex<float>> &zadoff_chu, const float zc_energy, std::vector<float> &plato, float threshold)
    {
        auto N = zadoff_chu.size();
        auto L = for_ofdm.size();
        static std::vector<float> r_re(L);
        static std::vector<float> r_im(L);
        static std::vector<float> zc_re(N);
        static std::vector<float> zc_im(N);

        split_to_float(for_ofdm.data(), r_re.data(), r_im.data(), r_im.size());
        split_to_float(zadoff_chu.data(), zc_re.data(), zc_im.data(), zc_im.size());

        float max_norm = -1.f;
        int best_idx = -1;

        for (size_t n = 0; n <= L - N; ++n)
        {
            float sum_re = 0.0f;
            float sum_im = 0.0f;
            float sig_energy = 0.0f;

            for (size_t k = 0; k < N; ++k)
            {
                float sr = r_re[n + k];
                float si = r_im[n + k];
                float zr = zc_re[k];
                float zi = zc_im[k];

                sum_re += sr * zr + si * zi;
                sum_im += si * zr - sr * zi;

                sig_energy += sr * sr + si * si;
            }

            float corr = sum_re * sum_re + sum_im * sum_im;
            float norm = corr / (sig_energy * zc_energy + 1e-12f);

            plato[n] = norm;

            if (norm > max_norm && norm > threshold)
            {
                max_norm = norm;
                best_idx = n;
            }
        }

        return best_idx;
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

            if (metric > max_metric && metric > 0.85)
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

    void ofdm_equalize(const std::vector<std::complex<float>> &input, std::vector<std::complex<float>> &output, const CarrierGrid &grid, Modulation mod, std::vector<std::complex<float>> &channel)
    {
        if (input.size() != grid.n)
            throw std::invalid_argument("Equalizer input has the wrong FFT size");
        channel.assign(grid.n, std::complex<float>{});
        const std::complex<float> pilot = get_pilot(mod);
        float maximum_power = 0.0f;
        for (const size_t k : grid.pilots)
        {
            channel[k] = input[k] / pilot;
            maximum_power = std::max(maximum_power, std::norm(channel[k]));
        }
        for (size_t p = 0; p + 1 < grid.pilots.size(); ++p)
        {
            const size_t k1 = grid.pilots[p];
            const size_t k2 = grid.pilots[p + 1];
            if (k1 == grid.half_active && k2 == grid.n - grid.half_active)
                continue;
            const float phase1 = std::arg(channel[k1]);
            const float delta_phase = std::remainder(std::arg(channel[k2]) - phase1, tau);
            const float magnitude1 = std::abs(channel[k1]);
            const float delta_magnitude = std::abs(channel[k2]) - magnitude1;
            for (size_t k = k1 + 1; k < k2; ++k)
            {
                const float alpha = static_cast<float>(k - k1) / static_cast<float>(k2 - k1);
                channel[k] = std::polar(magnitude1 + alpha * delta_magnitude, phase1 + alpha * delta_phase);
            }
        }
        const float minimum_power = std::max(1e-20f, maximum_power * 1e-8f);
        output.clear();
        output.reserve(grid.data.size());
        for (const size_t k : grid.data)
        {
            std::complex<float> value{};
            if (std::norm(channel[k]) > minimum_power)
                value = input[k] / channel[k];
            output.push_back(value);
        }
    }

    std::vector<std::complex<float>> generate_zc(int L, int q)
    {
        std::vector<std::complex<float>> zc(L);

        for (int n = 0; n < L; ++n)
        {
            float phase = -pi * q * n * (n + 1) / L;
            zc[n] = std::exp(std::complex<float>(0, phase));
        }

        return zc;
    }

    std::vector<std::complex<float>> scaled_ifft_output(const FFTWPlan &ifft, size_t active_count)
    {
        float scale = 16384.0f / (4.0f * std::sqrt(static_cast<float>(active_count)));
        float peak = 0.0f;
        for (size_t i = 0; i < ifft.size; ++i)
            peak = std::max({ peak, std::abs(ifft.out[i][0]), std::abs(ifft.out[i][1]) });
        if (peak > 0.0f)
            scale = std::min(scale, 16384.0f * 0.95f / peak);

        std::vector<std::complex<float>> output(ifft.size);
        for (size_t i = 0; i < ifft.size; ++i)
            output[i] = { ifft.out[i][0] * scale, ifft.out[i][1] * scale };
        return output;
    }

    std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(const CarrierGrid &grid, FFTWPlan &ifft)
    {
        const size_t length = grid.active_count();
        size_t root = 5;
        while (std::gcd(length, root) != 1)
            ++root;
        const auto zc = generate_zc(length, root);
        ifft.clear();
        for (size_t i = 0; i < length; ++i)
        {
            const size_t k = i < grid.half_active ? grid.n - grid.half_active + i
                                                  : i - grid.half_active + 1;
            ifft.in[k][0] = zc[i].real();
            ifft.in[k][1] = zc[i].imag();
        }
        fftwf_execute(ifft.plan);
        auto output = scaled_ifft_output(ifft, grid.active_count());
        for (auto &sample : output)
            sample = { static_cast<float>(clip(sample.real())), static_cast<float>(clip(sample.imag())) };
        return output;
    }

    std::vector<std::complex<float>> ofdm_zadoff_chu_symbol(const CarrierGrid &grid)
    {
        FFTWPlan ifft(grid.n, false);
        return ofdm_zadoff_chu_symbol(grid, ifft);
    }

    float coarse_cfo(std::vector<std::complex<float>> &r, int max_index, int N, int Lcp, float fs)
    {
        if (max_index < 0)
            return 0;
        std::complex<float> P = 0.0f;
        for (int i = 0; i < Lcp; ++i)
            P += r[max_index + i] * std::conj(r[max_index + i + N]);

        float epsilon = std::arg(P) / (tau);

        float cfo_hz = epsilon * fs / N;

        for (size_t n = 0; n < r.size(); ++n)
        {
            float phase = tau * cfo_hz * n / fs;
            r[n] *= std::complex<float>(std::cos(phase), std::sin(phase));
        }

        return cfo_hz;
    }

    double cfo_est(std::span<const std::complex<float>> signal, size_t zc_start, size_t following_symbols, const CarrierGrid &grid, double fallback)
    {
        std::complex<float> correlation = 0.0;
        double total_energy = 0.0;
        auto accumulate = [&](size_t start)
        {
            if (start + grid.symbol_length() > signal.size())
                return;
            for (size_t i = grid.cp / 4; i < grid.cp; ++i)
            {
                const std::complex<float> prefix(signal[start + i]);
                const std::complex<float> tail(signal[start + i + grid.n]);
                correlation += std::conj(prefix) * tail;
                total_energy += 0.5 * (std::norm(prefix) + std::norm(tail));
            }
        };
        if (zc_start >= grid.cp)
            accumulate(zc_start - grid.cp);
        for (size_t s = 0; s < following_symbols; ++s)
            accumulate(zc_start + grid.n + s * grid.symbol_length());
        if (total_energy <= 1e-12 || std::abs(correlation) < 0.15 * total_energy)
            return fallback;
        return std::arg(correlation) / static_cast<double>(grid.n);
    }

    std::vector<std::complex<float>> generate_frame_header(DSP::OFDMConfig &config, FrameHeader frameheader, std::vector<int> &data, std::vector<int> &pilots)
    {
        size_t N = config.n_subcarriers;
        size_t Ncp = config.n_cp;
        static FFTWPlan ifft(N, false);
        std::vector<std::complex<float>> header;
        std::vector<std::complex<float>> bpsk_symbols(N);
        const auto pilot = get_pilot(Modulation::BPSK);
        auto raw = packFrameHeader(frameheader);
        auto bits = encode(raw);
        bits.resize(bpsk_symbols.size(), 0);

        scramble(bits);
        bpsk_mapper_3gpp(bits, bpsk_symbols);

        for (size_t i = 0; i < N; ++i)
        {
            ifft.in[i][0] = 0.0f;
            ifft.in[i][1] = 0.0f;
        }

        for (size_t k : pilots)
        {
            ifft.in[k][0] = pilot.real();
            ifft.in[k][1] = pilot.imag();
        }

        for (size_t i = 0; i < data.size(); ++i)
        {
            size_t k = data[i];

            ifft.in[k][0] = std::real(bpsk_symbols[i]);
            ifft.in[k][1] = std::imag(bpsk_symbols[i]);
        }

        fftwf_execute(ifft.plan);

        for (size_t n = 0; n < N; ++n)
        {
            ifft.out[n][0] *= 16384.0f / 20.9f;
            ifft.out[n][1] *= 16384.0f / 20.9f;
        }

        // Cyclic Prefix
        for (size_t n = N - Ncp; n < N; ++n)
            header.push_back(std::complex<float>(ifft.out[n][0], ifft.out[n][1]));

        // Data
        for (size_t n = 0; n < N; ++n)
            header.push_back(std::complex<float>(ifft.out[n][0], ifft.out[n][1]));

        return header;
    };

    void modulate(Modulation mod, const std::vector<uint8_t> &bits, std::vector<std::complex<float>> &symbols)
    {
        const size_t bps = get_bits_per_symbol(mod);
        if (bps == 0 || bits.size() % bps != 0)
            throw std::invalid_argument("Invalid modulation input");
        symbols.resize(bits.size() / bps);
        switch (mod)
        {
        case Modulation::BPSK:
            bpsk_mapper_3gpp(bits, symbols);
            break;
        case Modulation::QPSK:
            qpsk_mapper_3gpp(bits, symbols);
            break;
        case Modulation::QAM16:
            qam16_mapper_3gpp(bits, symbols);
            break;
        case Modulation::QAM64:
            qam64_mapper_3gpp(bits, symbols);
            break;
        }
    }

    void append_symbol(const std::vector<std::complex<float>> &symbol, size_t cp, std::vector<int16_t> &buffer)
    {
        for (size_t i = symbol.size() - cp; i < symbol.size(); ++i)
        {
            buffer.push_back(clip(symbol[i].real()));
            buffer.push_back(clip(symbol[i].imag()));
        }
        for (const auto &sample : symbol)
        {
            buffer.push_back(clip(sample.real()));
            buffer.push_back(clip(sample.imag()));
        }
    }

    class OFDMTransmitter {
        CarrierGrid grid_;
        FFTWPlan ifft_;
        std::vector<std::complex<float>> zadoff_chu_;
        size_t header_coded_bits_;
        size_t header_symbols_;

        void append_ofdm_symbol(std::span<const std::complex<float>> symbols, Modulation mod, std::vector<int16_t> &buffer)
        {
            ifft_.clear();
            const auto pilot = get_pilot(mod);
            for (const size_t k : grid_.pilots)
            {
                ifft_.in[k][0] = pilot.real();
                ifft_.in[k][1] = pilot.imag();
            }
            for (size_t i = 0; i < symbols.size(); ++i)
            {
                const size_t k = grid_.data[i];
                ifft_.in[k][0] = symbols[i].real();
                ifft_.in[k][1] = symbols[i].imag();
            }
            fftwf_execute(ifft_.plan);
            append_symbol(scaled_ifft_output(ifft_, grid_.active_count()), grid_.cp, buffer);
        }
      public:
        explicit OFDMTransmitter(const DSP::OFDMConfig &config)
            : grid_(config),
              ifft_(grid_.n, false),
              zadoff_chu_(ofdm_zadoff_chu_symbol(grid_, ifft_)),
              header_coded_bits_(encoded_header_length()),
              header_symbols_(ceil_div(header_coded_bits_, grid_.data.size()))
        {
        }

        bool matches(const DSP::OFDMConfig &config) const
        {
            return grid_.n == config.n_subcarriers && grid_.cp == config.n_cp && grid_.spacing == config.pilot_spacing;
        }

        void ofdm(const std::vector<uint8_t> &input, Modulation mod, std::vector<int16_t> &buffer)
        {
            buffer.clear();
            const size_t bps = get_bits_per_symbol(mod);
            if (bps == 0 || input.empty() || input.size() > max_payload_bits || std::any_of(input.begin(), input.end(), [](uint8_t bit)
                                                                                            { return bit > 1; }))
                throw std::invalid_argument("Invalid payload or payload exceeds 65535 bits");
            const size_t count = ceil_div(input.size(), grid_.data.size() * bps);
            if (count > max_data_symbols)
                throw std::invalid_argument("Payload exceeds the 15-symbol PHY header limit");

            FrameHeader header;
            header.modulation = mod;
            header.ofdm_symbols_count = static_cast<uint8_t>(count);
            header.bits_count = static_cast<uint16_t>(input.size());
            auto header_bits = encode(packFrameHeader(header));
            if (header_bits.size() != header_coded_bits_)
                throw std::runtime_error("Header encoder length changed");
            header_bits.resize(header_symbols_ * grid_.data.size(), 0);
            scramble(header_bits);
            std::vector<std::complex<float>> header_symbols;
            modulate(Modulation::BPSK, header_bits, header_symbols);

            auto bits = input;
            scramble(bits);
            bits.resize(ceil_div(bits.size(), bps) * bps, 0);
            std::vector<std::complex<float>> symbols;
            modulate(mod, bits, symbols);
            buffer.reserve((1 + header_symbols_ + count) * grid_.symbol_length() * 2);
            append_symbol(zadoff_chu_, grid_.cp, buffer);
            for (size_t s = 0; s < header_symbols_; ++s)
                append_ofdm_symbol(std::span<const std::complex<float>>(header_symbols).subspan(s * grid_.data.size(), grid_.data.size()), Modulation::BPSK, buffer);
            for (size_t s = 0; s < count; ++s)
            {
                const size_t start = s * grid_.data.size();
                append_ofdm_symbol(std::span<const std::complex<float>>(symbols).subspan(start, std::min(grid_.data.size(), symbols.size() - start)), mod, buffer);
            }
        }
    };

    size_t correlation_fft_length(size_t signal_length, size_t reference_length)
    {
        const size_t required = signal_length + reference_length - 1;
        size_t result = 1;
        while (result < required)
        {
            if (result > static_cast<size_t>(std::numeric_limits<int>::max()) / 2)
                throw std::invalid_argument("Correlation FFT is too large");
            result *= 2;
        }
        return result;
    }

    class ZCCorrFFT {
      private:
        size_t reference_length_;
        size_t max_signal_length_;
        FFTWPlan forward_;
        FFTWPlan backward_;
        std::vector<std::complex<float>> reference_spectrum_;
        std::vector<double> metrics_;
        double reference_energy_ = 0.0;
      public:
        ZCCorrFFT(size_t signal_length, const std::vector<std::complex<float>> &reference)
            : reference_length_(reference.size()),
              max_signal_length_(signal_length),
              forward_(correlation_fft_length(signal_length, reference.size())),
              backward_(forward_.size, false),
              reference_spectrum_(forward_.size)
        {
            forward_.clear();
            for (size_t i = 0; i < reference.size(); ++i)
            {
                forward_.in[i][0] = reference[i].real();
                forward_.in[i][1] = reference[i].imag();
                reference_energy_ += std::norm(std::complex<float>(reference[i]));
            }
            if (reference_energy_ <= 0.0)
                throw std::invalid_argument("Zero-energy synchronization reference");
            fftwf_execute(forward_.plan);
            for (size_t i = 0; i < forward_.size; ++i)
                reference_spectrum_[i] = { forward_.out[i][0], -forward_.out[i][1] };
        }

        int find_peak(std::span<const std::complex<float>> signal, double omega, float threshold)
        {
            if (signal.size() <= reference_length_)
                return -1;
            if (signal.size() > max_signal_length_)
                throw std::invalid_argument("Correlation input exceeds the planned size");

            forward_.clear();
            const std::complex<double> step = std::polar(1.0, -omega);
            std::complex<float> rotation = 1.0;
            for (size_t i = 0; i < signal.size(); ++i)
            {
                const std::complex<float> value = std::complex<float>(signal[i]) * rotation;
                forward_.in[i][0] = static_cast<float>(value.real());
                forward_.in[i][1] = static_cast<float>(value.imag());
                rotation *= step;
            }
            fftwf_execute(forward_.plan);
            for (size_t i = 0; i < forward_.size; ++i)
            {
                const std::complex<float> value = std::complex<float>(forward_.out[i][0], forward_.out[i][1]) * reference_spectrum_[i];
                backward_.in[i][0] = value.real();
                backward_.in[i][1] = value.imag();
            }
            fftwf_execute(backward_.plan);

            const size_t count = signal.size() - reference_length_ + 1;
            metrics_.resize(count);
            double energy = 0.0;
            for (size_t i = 0; i < reference_length_; ++i)
                energy += std::norm(std::complex<float>(signal[i]));
            const double normalization = 1.0 / static_cast<double>(forward_.size);
            for (size_t i = 0; i < count; ++i)
            {
                if (i != 0)
                {
                    energy += std::norm(std::complex<float>(signal[i + reference_length_ - 1])) - std::norm(std::complex<float>(signal[i - 1]));
                    energy = std::max(energy, 0.0);
                }
                const std::complex<float> corr(backward_.out[i][0] * normalization, backward_.out[i][1] * normalization);
                metrics_[i] = energy > 1e-12 ? std::norm(corr) / (energy * reference_energy_) : 0.0;
            }
            for (size_t i = 0; i + 1 < count; ++i)
                if (metrics_[i] >= threshold && (i == 0 || metrics_[i] >= metrics_[i - 1]) && metrics_[i] >= metrics_[i + 1])
                    return static_cast<int>(i);
            return -1;
        }
    };
} // namespace

int run_dsp_rx(SharedData &data)
{
    auto &dsp = data.dsp;
    dsp.sample_rate = data.sdr.get_sample_rate();
    auto buff_size = data.sdr.get_buffer_size();
    FrameHeader header;
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

        int zc_idx = zc_corr_fft.find_peak(for_processing, 0.0, 0.3f);

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
            float cfo_hz = data.snap.cfo;
            float fs = data.sdr.get_sample_rate();
            for (size_t n = 0; n < for_processing.size(); ++n)
            {
                float phase = tau * cfo_hz * n / fs;
                for_processing[n] *= - std::complex<float>(std::cos(phase), std::sin(phase));
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

        ofdm_equalize(header_symbols_raw, header_symbols_equalized, grid, Modulation::BPSK, channel_est);
        demodulate(Modulation::BPSK, header_symbols_equalized, header_bits, llr);

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
        demodulate(dsp.ofdm_cfg.mod, equalized, bits, llr);
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

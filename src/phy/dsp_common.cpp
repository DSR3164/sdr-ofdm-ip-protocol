#include "phy/dsp_common.hpp"

#include <stdexcept>

std::mutex &fftw_mutex()
{
    static std::mutex mutex;
    return mutex;
}

FFTWPlan::FFTWPlan(int size, bool direction)
    : size(size)
{
    if (size == 0 || size > std::numeric_limits<int>::max())
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

FFTWPlan::~FFTWPlan()
{
    std::scoped_lock lock(fftw_mutex());
    fftwf_destroy_plan(plan);
    fftwf_free(in);
    fftwf_free(out);
}

void FFTWPlan::clear()
{
    for (size_t i = 0; i < size; ++i)
    {
        in[i][0] = 0.0f;
        in[i][1] = 0.0f;
    }
}

CarrierGrid::CarrierGrid(const DSP::OFDMConfig &config)
    : n(config.n_subcarriers),
      cp(config.n_cp),
      spacing(config.pilot_spacing),
      num_rbs(6)
{
    if (n == 128)
        num_rbs = 6;
    else if (n == 256)
        num_rbs = 15;
    else if (n == 512)
        num_rbs = 25;
    else if (n == 1024)
        num_rbs = 50;
    else if (n == 2048)
        num_rbs = 100;
    else
        throw std::invalid_argument("Unsupported FFT size. Use 128, 256, 512, 1024, or 2048.");

    size_t active_subcarriers = num_rbs * 12;
    size_t half_active = active_subcarriers / 2;

    is_guard.assign(n, true);

    auto assign_carrier = [&](size_t fft_idx, size_t v_idx)
    {
        is_guard[fft_idx] = false;

        if (v_idx % spacing == 0 || fft_idx == half_active || fft_idx == n - half_active || fft_idx == n - 1)
            pilots.push_back(fft_idx);
        else
            data.push_back(fft_idx);
    };

    size_t idx = 0;

    for (size_t k = 1; k <= half_active; ++k)
        assign_carrier(k, idx++);

    for (size_t k = n - half_active; k < n; ++k)
        assign_carrier(k, idx++);

    if (data.empty() || pilots.empty())
        throw std::invalid_argument("OFDM grid mapping failed");
}

void scramble(std::vector<uint8_t> &bits, uint8_t seed)
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

void descramble(std::vector<uint8_t> &bits, uint8_t seed)
{
    scramble(bits, seed);
}

void descramble_llr(std::vector<float> &llr_values, uint8_t seed)
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

std::vector<uint8_t> packFrameHeader(const DSPFrameHeader &header)
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

bool unpackFrameHeader(const std::vector<uint8_t> &bits, const CarrierGrid &grid, DSPFrameHeader &header)
{
    DSPFrameHeader candidate;
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
    const size_t half = length / 2;
    size_t root = 5;
    while (std::gcd(length, root) != 1)
        ++root;

    const auto zc = generate_zc(length, root);
    ifft.clear();

    for (size_t i = 0; i < half; ++i)
    {
        size_t k = 1 + i;
        ifft.in[k][0] = zc[half + i].real();
        ifft.in[k][1] = zc[half + i].imag();
    }

    for (size_t i = 0; i < half; ++i)
    {
        size_t k = grid.n - half + i;
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

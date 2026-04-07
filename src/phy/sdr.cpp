#include "logger.hpp"
#include "phy/sdr.hpp"

#include <SoapySDR/Logger.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>

void soapy_log_handler(const SoapySDRLogLevel logLevel, const char *message)
{
    switch (logLevel)
    {
    case SOAPY_SDR_FATAL: logs::sdr.critical("SoapySDR: {}", message); break;
    case SOAPY_SDR_ERROR: logs::sdr.error("SoapySDR: {}", message); break;
    case SOAPY_SDR_WARNING: logs::sdr.warn("SoapySDR: {}", message); break;
    case SOAPY_SDR_INFO: logs::sdr.info("SoapySDR: {}", message); break;
    default: logs::sdr.debug("SoapySDR: {}", message); break;
    }
}

SDR::SDR(const SDRConfig &config)
{
    SoapySDR::registerLogHandler(soapy_log_handler);
    cfg = config;
    auto list = SoapySDR::Device::enumerate();
    if (!list.empty() and (list[0]["uri"] != "ip:pluto.local"))
    {
        args = list[0];
        logs::sdr.info("Found SDR: {}", args["uri"]);
        if (cfg.init_on_start)
            flags |= Flags::IS_ACTIVE;
    }
}

/*!
 * \brief Initialize SDR device and configure RX/TX streams.
 *
 * Creates a SoapySDR device using internally stored arguments (`args`),
 * applies configuration from `cfg` (sample rate, frequency, gain),
 * and initializes RX and/or TX streams in CS16 format.
 *
 * Streams are activated immediately after creation.
 *
 * \return true on success, false if device creation failed.
 */
bool SDR::init()
{
    if (cfg.enable_rx and cfg.enable_tx)
        SDR::add_args();
    sdr = SoapySDR::Device::make(args);

    if (!sdr)
    {
        logs::sdr.error("Failed to create SDR: {}", args["uri"]);
        return 0;
    }

    // RX parameters
    sdr->setSampleRate(RX, 0, cfg.sample_rate);
    sdr->setFrequency(RX, 0, cfg.rx_freq);
    sdr->setGain(RX, 0, cfg.rx_gain);
    // sdr->setGainMode(RX, 0, false);
    // sdr->setBandwidth(RX, 0, rx_bandwidth);

    // TX parameters
    sdr->setSampleRate(TX, 0, cfg.sample_rate);
    sdr->setFrequency(TX, 0, cfg.tx_freq);
    sdr->setGain(TX, 0, cfg.tx_gain);
    // sdr->setGainMode(TX, 0, false);
    // sdr->setBandwidth(TX, 0, tx_bandwidth);

    // sdr->setDCOffsetMode(RX, 0, true);
    // sdr->setDCOffsetMode(TX, 0, true);
    // sdr->setIQBalanceMode(RX, 0, true);
    // sdr->setIQBalanceMode(TX, 0, true);

    // Stream parameters
    std::vector<size_t> channels = { 0 };
    if (cfg.enable_rx)
    {
        rxStream = sdr->setupStream(RX, SOAPY_SDR_CS16, channels);
        if (sdr->activateStream(rxStream, 0, 0, 0) == 0)
            logs::sdr.info("{} stream is active", fmt::format(fg(fmt::color::cyan), "RX"));
    }
    if (cfg.enable_tx)
    {
        txStream = sdr->setupStream(TX, SOAPY_SDR_CS16, channels);
        if (sdr->activateStream(txStream, 0, 0, 0) == 0)
            logs::sdr.info("{} stream is active", fmt::format(fg(fmt::color::cyan), "TX"));
    }
    logs::sdr.info("Create SDR: {}", args["uri"]);

    return true;
}

/*!
 * \brief Read samples from SDR RX stream.
 *
 * Reads interleaved IQ samples (CS16) into the provided buffer.
 * The buffer must be preallocated with at least `cfg.buffer_size` elements.
 *
 * \param[out] recv Buffer for received samples.
 * \return Number of elements read, or negative value on error.
 */
int SDR::readstream(std::vector<int16_t> &recv)
{
    void *rxbuffs[] = { recv.data() };

    int ret = sdr->readStream(
        rxStream,
        rxbuffs,
        cfg.buffer_size,
        sdr_flags,
        timeNs,
        timeoutUs);

    return ret;
};

/*!
 * \brief Write samples to SDR TX stream.
 *
 * Sends interleaved IQ samples (CS16) from the provided buffer.
 * The buffer must contain at least `cfg.buffer_size` elements.
 *
 * \param[in] send Buffer with samples to transmit.
 * \return Number of elements written, or negative value on error.
 */
int SDR::writestream(std::vector<int16_t> &send)
{
    void *txbuffs[] = { send.data() };

    int ret = sdr->writeStream(
        txStream,
        txbuffs,
        cfg.buffer_size,
        sdr_flags,
        timeNs + (4 * 1000 * 1000),
        timeoutUs);

    return ret;
};

/*!
 * \brief Deinitialize SDR device and release resources.
 *
 * Deactivates and closes RX/TX streams (if active),
 * then destroys the underlying SoapySDR device.
 *
 * Safe to call multiple times.
 *
 * \return true if deinitialization was performed, false if device was not initialized.
 */
bool SDR::deinit()
{
    if (sdr == nullptr)
        return false;

    if (sdr)
    {
        if (rxStream)
        {
            sdr->deactivateStream(rxStream, 0, 0);
            sdr->closeStream(rxStream);
            rxStream = nullptr;
        }
        if (txStream)
        {
            sdr->deactivateStream(txStream, 0, 0);
            sdr->closeStream(txStream);
            txStream = nullptr;
        }
        logs::sdr.info("Delete SDR: {}", args["uri"]);
        SoapySDR::Device::unmake(sdr);
        sdr = nullptr;
    }
    return true;
}

/*!
 * \brief Reinitialize SDR device if requested.
 *
 * If the REINIT flag is set, performs full deinitialization
 * followed by initialization and clears the flag.
 *
 * \return true if reinitialization was performed successfully,
 *         false otherwise.
 */
bool SDR::reinit()
{
    if (!has_flag(flags, Flags::REINIT))
        return false;
    if (SDR::deinit())
        if (SDR::init())
        {
            flags &= ~Flags::REINIT;
            return true;
        }
    return false;
}

/*!
 * \brief Add driver-specific arguments to SoapySDR configuration.
 *
 * Modifies internal `args` with parameters required by the driver
 * (e.g. direct buffer mode, timestamping, loopback).
 * Also sets stream flags and timeout.
 *
 * \return Always returns 0.
 */
int SDR::add_args()
{
    args["direct"] = "1";
    args["timestamp_every"] = "1920";
    args["loopback"] = "0";
    sdr_flags = SOAPY_SDR_HAS_TIME;
    timeoutUs = 400000;
    return 0;
}

/*!
 * \brief Apply runtime configuration changes to SDR device.
 *
 * Applies pending configuration updates based on internal flags:
 * - frequency (TX/RX)
 * - bandwidth (TX/RX)
 * - gain (TX/RX)
 * - sample rate
 *
 * After applying each parameter, the corresponding flag is cleared.
 *
 * Does nothing if device is not initialized.
 */
void SDR::apply_runtime()
{
    if (!sdr)
        return;

    if ((flags & Flags::APPLY_FREQUENCY) != Flags::None)
    {

        sdr->setFrequency(TX, 0, cfg.tx_freq);
        sdr->setFrequency(RX, 0, cfg.rx_freq);
        flags &= ~Flags::APPLY_FREQUENCY;
    }

    if ((flags & Flags::APPLY_BANDWIDTH) != Flags::None)
    {
        sdr->setBandwidth(TX, 0, cfg.tx_bandwidth);
        sdr->setBandwidth(RX, 0, cfg.rx_bandwidth);
        flags &= ~Flags::APPLY_BANDWIDTH;
    }

    if ((flags & Flags::APPLY_GAIN) != Flags::None)
    {
        sdr->setGain(TX, 0, cfg.tx_gain);
        sdr->setGain(RX, 0, cfg.rx_gain);
        flags &= ~Flags::APPLY_GAIN;
    }

    if ((flags & Flags::APPLY_SAMPLE_RATE) != Flags::None)
    {
        sdr->setSampleRate(RX, 0, cfg.sample_rate);
        sdr->setSampleRate(TX, 0, cfg.sample_rate);
        flags &= ~Flags::APPLY_SAMPLE_RATE;
    }
}

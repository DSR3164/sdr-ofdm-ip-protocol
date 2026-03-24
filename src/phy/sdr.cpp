#include "phy/sdr.hpp"

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>

SDR::SDR(const SDRConfig &config)
{
    cfg = config;
    auto list = SoapySDR::Device::enumerate();
    if (!list.empty())
    {
        args = list[0];
        for (auto &x : args)
            std::cout << x.first << "\t" << x.second << "\n";
        flags |= Flags::IS_ACTIVE;
    }
}

/*!
* \brief Initialize SDR device and configure RX/TX streams.
*
* Creates a SoapySDR device using parameters from `config`,
* sets sample rate, frequency, gain, bandwidth,
* and activates RX and/or TX streams in CS16 format.
*
* \param config Pointer to SDR configuration structure.
* \return 0 on success, non-zero on failure.
*/
bool SDR::init()
{
    if (cfg.enable_rx and cfg.enable_tx)
        SDR::add_args();
    sdr = SoapySDR::Device::make(args);

    if (!sdr)
    {
        std::cerr << "Failed to create SDR " << args["uri"] << "\n";
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
        sdr->activateStream(rxStream, 0, 0, 0);
        std::cout << "\nActivate RX Stream" << "\n";
    }
    if (cfg.enable_tx)
    {
        txStream = sdr->setupStream(TX, SOAPY_SDR_CS16, channels);
        sdr->activateStream(txStream, 0, 0, 0);
        std::cout << "\nActivate TX Stream" << "\n";
    }
    std::cout << "\nCreate SDR:" << args["uri"] << "\n";

    return 1;
}

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

int SDR::writestream(std::vector<int16_t> &send)
{
    void *txbuffs[] = { send.data() };

    int ret = sdr->writeStream(
        txStream,
        txbuffs,
        cfg.buffer_size,
        sdr_flags,
        timeNs,
        timeoutUs);

    return ret;
};

/*!
 * \brief Deinitialize SDR device and release resources.
 *
 * Deactivates and closes RX/TX streams, then destroys
 * the associated SoapySDR device instance.
 *
 * \param config Pointer to SDR configuration structure.
 * \return 0 on completion.
*/
bool SDR::deinit()
{
    if (sdr != nullptr)
        return 0;

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
        std::cout << "\nDelete SDR:" << args["uri"] << "\n";
        SoapySDR::Device::unmake(sdr);
        sdr = nullptr;
    }
    return 1;
}

/*!
 * \brief Reinitialize SDR device.
 *
 * Performs full deinitialization and reinitialization
 * of the SDR backend and clears the REINIT flag.
 *
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
 * Inserts runtime parameters such as direct buffer mode,
 * timestamp generation interval, and loopback control.
 *
 * \param args SoapySDR keyword arguments structure to modify.
 * \return 0 on success.
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
* Updates frequency, bandwidth, gain, and sample rate
* according to active flags in `context`.
* Each applied parameter clears its corresponding flag.
*
* \param context SDR configuration structure.
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

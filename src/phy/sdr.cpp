#include "logger.hpp"
#include "phy/sdr.hpp"

#include <uhd/types/device_addr.hpp>

SDR::SDR(const SDRConfig &config, std::atomic<bool> &stop_condition)
    : cond(stop_condition),
      cfg(config)
{
    scan();
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
    if (cond.load())
        return false;

    usrp = uhd::usrp::multi_usrp::make(args);

    if (!usrp)
    {
        logs::sdr.error("Failed to create USRP");
        return false;
    }

    // RX
    usrp->set_rx_rate(cfg.sample_rate, 0);
    usrp->set_rx_freq(cfg.rx_freq, 0);
    usrp->set_rx_gain(cfg.rx_gain, 0);
    usrp->set_rx_bandwidth(cfg.rx_bandwidth, 0);

    // TX
    usrp->set_tx_rate(cfg.sample_rate, 0);
    usrp->set_tx_freq(cfg.tx_freq, 0);
    usrp->set_tx_gain(cfg.tx_gain, 0);
    usrp->set_tx_bandwidth(cfg.tx_bandwidth, 0);

    if (cfg.enable_rx)
    {
        uhd::stream_args_t stream_args;
        stream_args.cpu_format = "sc16";
        stream_args.otw_format = "sc16";
        stream_args.channels = { 0 };

        rxStream = usrp->get_rx_stream(stream_args);
    }

    if (cfg.enable_tx)
    {
        uhd::stream_args_t stream_args;
        stream_args.cpu_format = "sc16";
        stream_args.otw_format = "sc16";
        stream_args.channels = { 0 };

        txStream = usrp->get_tx_stream(stream_args);

        logs::sdr.info(
            "{} stream is active",
            fmt::format(fg(fmt::color::cyan), "TX")
        );
    }

    flags |= Flags::IS_ACTIVE;

    return true;
}

void SDR::start_rx()
{
    uhd::stream_cmd_t stream_cmd(
        uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS
    );

    stream_cmd.stream_now = true;

    rxStream->issue_stream_cmd(stream_cmd);

    logs::sdr.info(
        "{} stream is active",
        fmt::format(fg(fmt::color::cyan), "RX")
    );
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
    uhd::rx_metadata_t metadata;

    const size_t samples = recv.size() / 2;

    size_t ret = rxStream->recv(
        recv.data(),
        samples,
        metadata,
        timeout,
        false
    );

    if (metadata.error_code != uhd::rx_metadata_t::ERROR_CODE_NONE)
    {
        logs::sdr.error(
            "RX error: {}",
            metadata.strerror()
        );

        return -1;
    }

    return static_cast<int>(ret);
}

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
    uhd::tx_metadata_t metadata;
    metadata.start_of_burst = true;
    metadata.end_of_burst = true;
    metadata.has_time_spec = false;

    const size_t samples = send.size() / 2;

    size_t ret = txStream->send(
        send.data(),
        samples,
        metadata,
        timeout
    );

    logs::sdr.info("TX requested {}, sent {}", samples, ret);

    return static_cast<int>(ret);
}

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
    if (usrp == nullptr)
        return false;

    if (rxStream)
    {
        uhd::stream_cmd_t stream_cmd(
            uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS
        );

        rxStream->issue_stream_cmd(stream_cmd);

        rxStream = nullptr;
    }

    if (txStream)
    {
        txStream = nullptr;
    }

    logs::sdr.info(
        "Delete USRP: {}",
        args.to_string()
    );

    usrp.reset();

    flags &= ~Flags::IS_ACTIVE;
    flags &= ~Flags::FOUND;

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

void SDR::scan()
{
    auto list = uhd::device::find(uhd::device_addr_t{});

    if (!list.empty())
    {
        args = list[0];

        logs::sdr.info(
            "Found USRP: {}",
            args.to_string()
        );

        if (cfg.init_on_start)
            flags |= Flags::FOUND;
    }
}

void SDR::wait_connection()
{
    while (!has_flag(flags, Flags::FOUND))
    {
        if (connection_retries == 1)
            logs::sdr.warn("No SDR devices detected. Waiting for any available connection...");
        if (cond.load())
        {
            logs::sdr.info("Closing SDR thread");
            return;
        }
        scan();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        connection_retries++;
    }
    connection_retries = 0;
}

bool SDR::check_connection()
{
    if (!usrp || !has_flag(flags, Flags::IS_ACTIVE))
        return false;

    auto msg = fmt::format(
        fg(fmt::color::green),
        "waiting for connection..."
    );

    logs::sdr.warn(
        "USRP seems to be disconnected, trying to find it"
    );

    auto found = uhd::device::find(args);

    if (found.empty())
    {
        if (cfg.exit_on_error)
        {
            msg = fmt::format(
                fg(fmt::color::red),
                "closing application"
            );

            logs::sdr.critical(
                "USRP was disconnected, {}",
                msg
            );

            return false;
        }

        if (deinit())
        {
            logs::sdr.critical(
                "USRP was disconnected, {}",
                msg
            );

            wait_connection();

            if (!init())
                return false;
        }
    }

    return true;
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
    timeout = 0.4;
    time_delay_ns = 2e6;

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
    if (!usrp)
        return;

    if ((flags & Flags::APPLY_FREQUENCY) != Flags::None)
    {
        usrp->set_tx_freq(cfg.tx_freq, 0);
        usrp->set_rx_freq(cfg.rx_freq, 0);

        flags &= ~Flags::APPLY_FREQUENCY;
    }

    if ((flags & Flags::APPLY_BANDWIDTH) != Flags::None)
    {
        usrp->set_tx_bandwidth(cfg.tx_bandwidth, 0);
        usrp->set_rx_bandwidth(cfg.rx_bandwidth, 0);

        flags &= ~Flags::APPLY_BANDWIDTH;
    }

    if ((flags & Flags::APPLY_GAIN) != Flags::None)
    {
        usrp->set_tx_gain(cfg.tx_gain, 0);
        usrp->set_rx_gain(cfg.rx_gain, 0);

        flags &= ~Flags::APPLY_GAIN;
    }

    if ((flags & Flags::APPLY_SAMPLE_RATE) != Flags::None)
    {
        usrp->set_rx_rate(cfg.sample_rate, 0);
        usrp->set_tx_rate(cfg.sample_rate, 0);

        flags &= ~Flags::APPLY_SAMPLE_RATE;
    }
}

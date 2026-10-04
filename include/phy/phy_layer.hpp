#pragma once

#include "common.hpp"

#include <SoapySDR/Device.hpp>

// int run_sdr(SharedData &data);
int run_sdr_rx(SharedData &data);
int run_sdr_tx(SharedData &data);
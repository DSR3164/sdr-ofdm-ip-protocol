#pragma once

#include "common.hpp"
#include "sockets.hpp"

#include <SoapySDR/Device.hpp>

int run_sdr(SharedData &data);

int run_dsp_gui_bridge(SharedData &data, socketData &socket);

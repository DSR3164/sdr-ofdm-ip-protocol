#pragma once

#include "sockets.hpp"
#include "buffers.hpp"

inline float lat = 1.0f;

struct ControlSignals {
    bool gui_run{ false };
};

// Backend Bridges
int run_control_bridge_client(SharedData &data, socketData &socket);

int run_ip_gui_bridge(SharedData &data, socketData &socket);

int run_dsp_gui_bridge(SharedData &data, socketData &socket);

int run_dsp_stats_bridge(SharedData &data, socketData &socket);

void run_gui_bridge_supervisor(SharedData &data, socketData &socket);

// GUI Bridges
int run_control_bridge_server(socketData &socket);

int run_ip_brigde(Buffers &data, socketData &socket);

int run_dsp_bridge(Buffers &bufs, socketData &socket);

int run_stats_bridge(Buffers &bufs, socketData &socket);

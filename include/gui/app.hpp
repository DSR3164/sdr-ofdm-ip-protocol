#pragma once

#include "common.hpp"

#include <SDL2/SDL.h>
#include <string>
#include <span>
#include "implot.h"

struct Buffers
{
    DoubleBuffer<int16_t> sdr_raw;
    DoubleBuffer<std::complex<float>> dsp;

    Buffers(int size1 = 3840, int size2 = 3840)
        : sdr_raw(size1),
        dsp(size2)
    {
    }
};

class App {
    public:
    App(const std::string &title, int width, int height);
    ~App();

    bool is_open() { return running; }
    bool is_debug_run() { return debug_run; }
    bool is_gui_run() { return gui_run; }
    bool is_phy_run() { return phy_run; }
    bool is_ip_run() { return ip_run; }
    void start_frame();
    void stop_frame();
    void control_wd();
    void begin_debug();
    void set_vsync_state(bool vsync_state) { (vsync_state) ? SDL_GL_SetSwapInterval(1) : SDL_GL_SetSwapInterval(0); }

    template <typename T, typename R>
    void begin_plot_1d(const std::string &label, std::span<const R> data)
    {
        PlotSpec<T> plot_1d;
        if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail())))
        {
            ImPlot::PlotLine(label.c_str(), data.data(), (int)data.size(), 1.0, 0.0, plot_1d.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T, typename R>
    void begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, std::span<const R> data)
    {
        PlotSpec<T> plot_2d(2);
        int count = data.size() / 2;
        const T *raw_ptr = reinterpret_cast<const T *>(data.data());
        if (ImPlot::BeginPlot(label.c_str(), ImGui::GetContentRegionAvail()))
        {
            ImPlot::PlotLine(label_i.c_str(), raw_ptr, count, 1.0, 0.0, plot_2d.spec);
            ImPlot::PlotLine(label_q.c_str(), raw_ptr + 1, count, 1.0, 0.0, plot_2d.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T, typename R>
    void begin_scatter(const std::string &label, std::span<const R> data)
    {
        PlotSpec<T> plot_scatter(2, ImPlotMarker_Square, 1.0f);
        const T *raw_ptr = reinterpret_cast<const T *>(data.data());
        int count = data.size() / 2;

        if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail()), ImPlotFlags_Equal))
        {
            ImPlot::PlotScatter(label.c_str(), raw_ptr, raw_ptr + 1, count, plot_scatter.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T>
    class PlotSpec
    {
        public:
        ImPlotSpec spec;
        PlotSpec(int stride_elements = 1, ImPlotMarker marker = ImPlotMarker_None, float marker_size = 2.0f)
        {
            spec.Marker = marker;
            spec.MarkerSize = marker_size;
            spec.Stride = sizeof(T) * stride_elements;
            spec.Offset = 0;
        }
    };

    private:
    SDL_Window *window;
    SDL_GLContext gl_context;
    bool running = true;
    bool debug_run = false;
    bool vsync_state = true;
    bool gui_run = false;
    bool phy_run = false;
    bool ip_run = false;
};

void run_gui(Buffers &sd);
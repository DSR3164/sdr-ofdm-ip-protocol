#pragma once

#include "sockets.hpp"
#include "buffers.hpp"

#include <SDL2/SDL.h>
#include <deque>
#include <implot.h>
#include <span>
#include <string>
#include <type_traits>

template <typename T>
struct is_complex : std::false_type {
};

template <typename T>
struct is_complex<std::complex<T>> : std::true_type {
};

struct WaterfallData {
    int fft_size;
    int history_rows;
    std::vector<float> data;

    fftwf_complex *fft_in;
    fftwf_complex *fft_out;
    fftwf_plan fft_plan;
    std::vector<float> window;

    std::chrono::steady_clock::time_point last_update;
    int update_interval_ms;

    WaterfallData(int fft_sz = 512, int rows = 300);
    ~WaterfallData();

    void process_samples(const std::vector<std::complex<float>> &samples);
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
    bool is_chos_sock() { return choose_socket; }
    void setup_theme();
    void setup_plotTheme();
    void start_frame();
    void stop_frame();
    void control_wd(std::vector<std::string> &sockets, socketData &sock);
    void begin_debug(Buffers &data);
    void run_stats_plot(const std::deque<StatsSnapshot> &stats);
    void run_heatmap(const std::string &label, const float *data, int rows, int cols, float scale_min, float scale_max);
    void run_waterfall(const std::string &label, WaterfallData &waterfall, const std::vector<std::complex<float>> &data);
    void set_vsync_state(bool vsync_state) { (vsync_state) ? SDL_GL_SetSwapInterval(1) : SDL_GL_SetSwapInterval(0); }

    bool choose_socket = false;
    int selected_socket_idx = -1;

    template <typename T, typename R>
    void begin_plot_1d(const std::string &label, std::span<const R> data)
    {
        PlotSpec<T> plot_1d;
        if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail())))
        {
            ImPlot::SetupAxesLimits(0, data.size(), 0.0, 1.0, ImPlotCond_Once);
            ImPlot::PlotLine(label.c_str(), data.data(), (int)data.size(), 1.0, 0.0, plot_1d.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T, typename R>
    void begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, std::span<const R> data)
    {
        PlotSpec<T> plot_2d(2);
        int count;
        if constexpr (is_complex<R>::value)
            count = data.size();
        else
            count = data.size() / 2;

        const T *raw_ptr = reinterpret_cast<const T *>(data.data());
        if (ImPlot::BeginPlot(label.c_str(), ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxesLimits(0, count, -1000.0, 1000.0, ImPlotCond_Once);
            ImPlot::PlotLine(label_i.c_str(), raw_ptr, count, 1.0, 0.0, plot_2d.spec);
            ImPlot::PlotLine(label_q.c_str(), raw_ptr + 1, count, 1.0, 0.0, plot_2d.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T, typename R>
    void begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, std::vector<R> data_1, std::vector<R> data_2)
    {
        PlotSpec<T> plot_2d;

        size_t count = data_2.size();
        data_1.resize(count);

        if (ImPlot::BeginPlot(label.c_str(), ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxesLimits(count * -0.2, count * 1.2, -0.2, 1.6, ImPlotCond_Once);
            ImPlot::PlotLine(label_i.c_str(), data_1.data(), count, 1.0, 0.0, plot_2d.spec);
            ImPlot::PlotLine(label_q.c_str(), data_2.data(), count, 1.0, 0.0, plot_2d.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T, typename R>
    void begin_scatter(const std::string &label, std::span<const R> data)
    {
        PlotSpec<T> plot_scatter(2, ImPlotMarker_Square, 1.0f);
        const T *raw_ptr = reinterpret_cast<const T *>(data.data());
        int count;
        if constexpr (is_complex<R>::value)
            count = data.size();
        else
            count = data.size() / 2;

        if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail()), ImPlotFlags_Equal))
        {
            ImPlot::SetupAxesLimits(-1.5, 1.5, -1.5, 1.5, ImPlotCond_Once);
            ImPlot::PlotScatter(label.c_str(), raw_ptr, raw_ptr + 1, count, plot_scatter.spec);
            ImPlot::EndPlot();
        }
    }

    template <typename T>
    class PlotSpec {
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
    bool control_menu_was_open = false;
    bool running = true;
    bool debug_run = true;
    bool vsync_state = true;
    bool gui_run = false;
    bool phy_run = true;
    bool ip_run = true;
};

socketData choose_socket(const std::string &sock);

void run_gui(Buffers &buf, std::vector<std::string> &sockets, socketData &sock, std::atomic_bool &quit);

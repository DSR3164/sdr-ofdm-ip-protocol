#include "gui/app.hpp"
#include "gui/gui_layer.hpp"
#include "gui/ip_dev.hpp"
#include "gui/phy_dev.hpp"

#include <GL/glew.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <fftw3.h>
#include <filesystem>
#include <imgui.h>
#include <string>
#include <vector>

App::App(const std::string &title, int width, int height)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    ImGui::CreateContext();
    ImPlot::CreateContext();

    std::filesystem::path exe_dir = std::filesystem::canonical(std::filesystem::path(SDL_GetBasePath()));
    static std::string ini_path = (exe_dir.parent_path() / "config" / "imgui.ini").string();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = ini_path.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    ImGui_ImplSDL2_InitForOpenGL(window, gl_context);
    ImGui_ImplOpenGL3_Init("#version 330");
}

App::~App()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl_context);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

void App::start_frame()
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);
        if (event.type == SDL_QUIT)
            running = false;
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());
}

void App::stop_frame()
{
    ImGui::Render();
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window);
}

void App::control_wd(std::vector<std::string> &sockets, socketData &sock)
{
    bool chosen_socket = false;

    if (ImGui::BeginMainMenuBar())
    {
        bool menu_open = ImGui::BeginMenu("Control Panel");

        if (menu_open)
        {
            if (!control_menu_was_open)
            {
                found_sockets(sockets);
                sock.cleanup_old_sockets();
            }

            control_menu_was_open = true;

            ImGui::SeparatorText("Video Settings");
            ImGui::MenuItem("VSYNC", nullptr, &vsync_state);
            this->set_vsync_state(vsync_state);

            ImGui::SeparatorText("Workstation");

            ImGui::MenuItem("GUI Dev", nullptr, &gui_run);
            ImGui::MenuItem("PHY Dev", nullptr, &phy_run);
            ImGui::MenuItem("IP Dev", nullptr, &ip_run);

            ImGui::SeparatorText("Socket Folders");
            static std::string last_socket_path = "None";

            if (ImGui::Button("Update sockets", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                found_sockets(sockets);

            if (ImGui::Button("Clear stole sockets", ImVec2(ImGui::GetContentRegionAvail().x, 0.0f)))
                sock.cleanup_old_sockets();

            if (sockets.empty())
                ImGui::Text("%s", last_socket_path.c_str());
            else
                for (int i = 0; i < sockets.size(); ++i)
                {
                    bool is_selected = (selected_socket_idx == i);
                    if (ImGui::MenuItem(sockets[i].c_str(), nullptr, is_selected))
                    {
                        selected_socket_idx = i;
                        this->choose_socket = true;
                    }
                }

            ImGui::SeparatorText("Debug");

            ImGui::MenuItem("Open debug panel", nullptr, &debug_run);

            ImGui::EndMenu();
        }
        else
        {
            control_menu_was_open = false;
        }
        ImGui::EndMainMenuBar();
    }
}

void App::begin_debug()
{
    ImGuiIO &io = ImGui::GetIO();
    if (ImGui::Begin("Debug Panel"))
    {
        ImGui::SeparatorText("Statistics");
        ImGui::Text("FPS: %.f (%0.3f ms)", io.Framerate, 1000.0f / io.Framerate);
    }
    ImGui::End();
}

void run_gui(Buffers &buf, std::vector<std::string> &sockets, socketData &sock)
{
    App app("Development", 1280, 720);

    while (app.is_open())
    {
        app.start_frame();
        app.control_wd(sockets, sock);

        if (app.is_chos_sock())
        {
            if (app.selected_socket_idx >= 0 && app.selected_socket_idx < sockets.size())
            {
                sock = choose_socket(sockets[app.selected_socket_idx]);
                logs::gui.info("Selected socket: {}", sockets[app.selected_socket_idx]);
            }
            app.choose_socket = false;
        }

        if (app.is_debug_run())
            app.begin_debug();

        if (app.is_gui_run())
            gui_dev(app);

        if (app.is_phy_run())
            phy_dev(app, buf);

        if (app.is_ip_run())
            ip_dev(app, buf);

        app.stop_frame();
    }
}

socketData choose_socket(const std::string &folder_name)
{
    socketData socket(false);

    std::filesystem::path base = "/tmp/" + folder_name;
    socket.socketPath = base.string();
    socket.ip_socket = "ipc://" + (base / "ip_gui.sock").string();
    socket.phy_socket = "ipc://" + (base / "dsp_gui.sock").string();
    socket.stats_socket = "ipc://" + (base / "stats.sock").string();

    return socket;
}

void App::run_heatmap(const std::string &label, const float *data, int rows, int cols, float scale_min = -80.0f, float scale_max = -20.0f)
{
    if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail())))
    {
        ImPlot::SetupAxes("Frequency", "Row");
        double half_cols = cols / 2.0;
        ImPlotPoint bounds_min(-half_cols, 0);
        ImPlotPoint bounds_max(half_cols, rows);
        ImPlot::PlotHeatmap("##heatmap", data, rows, cols, scale_min, scale_max, nullptr, bounds_min, bounds_max);
        ImPlot::EndPlot();
    }
}

void App::run_waterfall(const std::string &label, WaterfallData &waterfall, const std::vector<std::complex<float>> &data)
{
    waterfall.process_samples(data);

    ImGui::Text("FFT Size: %d", waterfall.fft_size);
    ImGui::Text("History: %d rows", waterfall.history_rows);
    ImGui::SliderInt("Update Rate(ms)", &waterfall.update_interval_ms, 5, 100);

    static float min_db = -40.0f;
    static float max_db = -10.0f;

    ImGui::SliderFloat("Min dB", &min_db, -200.0f, -40.0f);
    ImGui::SliderFloat("Max dB", &max_db, -39.99f, -0.0f);

    static int colormap_idx = 5;
    const char *colormap_names[] = { "Viridis", "Plasma", "Hot", "Cool", "Pink", "Jet" };
    const int colormap_values[] = {
        ImPlotColormap_Viridis,
        ImPlotColormap_Plasma,
        ImPlotColormap_Hot,
        ImPlotColormap_Cool,
        ImPlotColormap_Pink,
        ImPlotColormap_Jet
    };
    ImGui::Combo("Colormap", &colormap_idx, colormap_names, 6);
    ImPlot::PushColormap(colormap_values[colormap_idx]);
    run_heatmap(label, waterfall.data.data(), waterfall.history_rows, waterfall.fft_size, min_db, max_db);
    ImPlot::PopColormap();
}

WaterfallData::WaterfallData(int fft_sz, int rows)
    : fft_size(fft_sz),
      history_rows(rows),
      update_interval_ms(15)
{
    data.resize(fft_size * history_rows, -80.0f);
    window.resize(fft_size);
    for (int i = 0; i < fft_size; ++i)
        window[i] = 0.5f * (1.0f - std::cos(2.0f * M_PIf * i / (fft_size - 1)));
    fft_in = fftwf_alloc_complex(fft_size);
    fft_out = fftwf_alloc_complex(fft_size);
    fft_plan = fftwf_plan_dft_1d(fft_size, fft_in, fft_out, FFTW_FORWARD, FFTW_MEASURE);
    last_update = std::chrono::steady_clock::now();
}

WaterfallData::~WaterfallData()
{
    fftwf_destroy_plan(fft_plan);
    fftwf_free(fft_in);
    fftwf_free(fft_out);
}

void WaterfallData::process_samples(const std::vector<std::complex<float>> &samples)
{
    static std::vector<float> local_max_db(fft_size, -200.0f);
    static int accumulated_blocks = 0;

    if (samples.size() < fft_size) return;

    for (int i = 0; i < fft_size; ++i) {
        std::complex<float> windowed = samples[i] * window[i];
        fft_in[i][0] = windowed.real();
        fft_in[i][1] = windowed.imag();
    }
    fftwf_execute(fft_plan);

    const float norm = 1.0f / fft_size;
    const float norm_sq = norm * norm;
    const int half = fft_size / 2;

    for (int i = 0; i < fft_size; ++i) {
        int idx = (i + half) % fft_size;
        float re = fft_out[idx][0];
        float im = fft_out[idx][1];
        float db = 10.0f * std::log10f(norm_sq * (re * re + im * im) + 1e-12f);

        if (db > local_max_db[i]) {
            local_max_db[i] = db;
        }
    }
    accumulated_blocks++;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);

    if (elapsed.count() < update_interval_ms)
        return;

    last_update = now;

    if (accumulated_blocks == 0) return;

    std::memmove(data.data() + fft_size, data.data(), (history_rows - 1) * fft_size * sizeof(float));
    std::memcpy(data.data(), local_max_db.data(), fft_size * sizeof(float));

    std::fill(local_max_db.begin(), local_max_db.end(), -200.0f);
    accumulated_blocks = 0;
}

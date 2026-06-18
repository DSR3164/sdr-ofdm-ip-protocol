#include "gui/app.hpp"
#include "gui/gui_layer.hpp"
#include "gui/ip_dev.hpp"
#include "gui/phy_dev.hpp"

#include <GL/glew.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <fcntl.h>
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
    setup_theme();
    setup_plotTheme();
    std::filesystem::path exe_dir = std::filesystem::canonical(std::filesystem::path(SDL_GetBasePath()));
    static std::string ini_path = (exe_dir.parent_path() / "config" / "imgui.ini").string();
    ImGuiIO &io = ImGui::GetIO();
    io.Fonts->AddFontFromFileTTF((exe_dir.parent_path() / "resources" / "fonts" / "CascadiaMono-VariableFont_wght.ttf").string().c_str(), 16.0f);
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

void App::setup_theme()
{
    ImGuiStyle &style = ImGui::GetStyle();
    ImVec4 *colors = style.Colors;

    ImVec4 bg = ImVec4(0.09f, 0.10f, 0.11f, 1.00f);
    ImVec4 bg_panel = ImVec4(0.12f, 0.13f, 0.15f, 1.00f);
    ImVec4 bg_widget = ImVec4(0.16f, 0.17f, 0.19f, 1.00f);
    ImVec4 border = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    ImVec4 text = ImVec4(0.86f, 0.87f, 0.88f, 1.00f);
    ImVec4 text_dim = ImVec4(0.55f, 0.57f, 0.60f, 1.00f);
    ImVec4 accent = ImVec4(0.20f, 0.75f, 0.85f, 1.00f);
    ImVec4 accent_hi = ImVec4(0.30f, 0.85f, 0.95f, 1.00f);

    colors[ImGuiCol_WindowBg] = bg;
    colors[ImGuiCol_ChildBg] = bg;
    colors[ImGuiCol_PopupBg] = bg_panel;
    colors[ImGuiCol_Border] = border;
    colors[ImGuiCol_FrameBg] = bg_widget;
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.23f, 0.25f, 1.00f);
    colors[ImGuiCol_TitleBg] = bg;
    colors[ImGuiCol_TitleBgActive] = bg_panel;
    colors[ImGuiCol_TitleBgCollapsed] = bg;
    colors[ImGuiCol_MenuBarBg] = bg_panel;
    colors[ImGuiCol_ScrollbarBg] = bg;
    colors[ImGuiCol_ScrollbarGrab] = bg_widget;
    colors[ImGuiCol_ScrollbarGrabHovered] = border;
    colors[ImGuiCol_ScrollbarGrabActive] = accent;
    colors[ImGuiCol_CheckMark] = accent;
    colors[ImGuiCol_SliderGrab] = accent;
    colors[ImGuiCol_SliderGrabActive] = accent_hi;
    colors[ImGuiCol_Button] = bg_widget;
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_ButtonActive] = accent;
    colors[ImGuiCol_Header] = bg_widget;
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.20f, 0.21f, 0.23f, 1.00f);
    colors[ImGuiCol_HeaderActive] = accent;
    colors[ImGuiCol_Separator] = border;
    colors[ImGuiCol_SeparatorHovered] = accent;
    colors[ImGuiCol_SeparatorActive] = accent_hi;
    colors[ImGuiCol_ResizeGrip] = border;
    colors[ImGuiCol_ResizeGripHovered] = accent;
    colors[ImGuiCol_ResizeGripActive] = accent_hi;
    colors[ImGuiCol_Tab] = bg_panel;
    colors[ImGuiCol_TabHovered] = bg_widget;
    colors[ImGuiCol_TabActive] = bg_widget;
    colors[ImGuiCol_TabUnfocused] = bg;
    colors[ImGuiCol_TabUnfocusedActive] = bg_panel;
    colors[ImGuiCol_DockingPreview] = ImVec4(accent.x, accent.y, accent.z, 0.30f);
    colors[ImGuiCol_DockingEmptyBg] = bg;
    colors[ImGuiCol_Text] = text;
    colors[ImGuiCol_TextDisabled] = text_dim;

    style.WindowRounding = 3.0f;
    style.FrameRounding = 2.0f;
    style.PopupRounding = 3.0f;
    style.ScrollbarRounding = 2.0f;
    style.GrabRounding = 2.0f;
    style.TabRounding = 3.0f;

    style.WindowPadding = ImVec2(8, 8);
    style.FramePadding = ImVec2(6, 4);
    style.ItemSpacing = ImVec2(6, 4);
    style.ItemInnerSpacing = ImVec2(4, 4);
    style.ScrollbarSize = 12.0f;
    style.GrabMinSize = 8.0f;

    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
}

void App::setup_plotTheme()
{
    ImPlot::StyleColorsDark();
    ImPlotStyle &pstyle = ImPlot::GetStyle();

    pstyle.PlotBorderSize = 1.0f;

    pstyle.Colors[ImPlotCol_FrameBg] = ImVec4(0.09f, 0.10f, 0.11f, 1.0f);
    pstyle.Colors[ImPlotCol_PlotBg] = ImVec4(0.07f, 0.08f, 0.09f, 1.0f);
    pstyle.Colors[ImPlotCol_PlotBorder] = ImVec4(0.22f, 0.23f, 0.25f, 1.0f);
    pstyle.Colors[ImPlotCol_LegendBg] = ImVec4(0.12f, 0.13f, 0.15f, 0.9f);
    pstyle.Colors[ImPlotCol_AxisGrid] = ImVec4(0.22f, 0.23f, 0.25f, 0.5f);

    static const ImVec4 telemetry_map[] = {
        ImVec4(0.20f, 0.75f, 0.85f, 1.0f),
        ImVec4(0.95f, 0.55f, 0.20f, 1.0f),
        ImVec4(0.55f, 0.85f, 0.35f, 1.0f),
        ImVec4(0.85f, 0.35f, 0.55f, 1.0f),
        ImVec4(0.75f, 0.75f, 0.30f, 1.0f),
    };
    ImPlot::AddColormap("Telemetry", telemetry_map, 5);
    pstyle.Colormap = ImPlot::GetColormapIndex("Telemetry");
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
            control_menu_was_open = false;
        ImGui::EndMainMenuBar();
    }
}

void App::begin_debug(Buffers &data)
{
    ImGuiIO &io = ImGui::GetIO();

    // Stats
    static StatsSnapshot snapshot{};
    static std::deque<StatsSnapshot> stats;
    std::vector<uint8_t> raw_stats;

    if (data.stats.read(raw_stats) == 0)
        if (raw_stats.size() == sizeof(StatsSnapshot))
        {
            std::memcpy(&snapshot, raw_stats.data(), sizeof(StatsSnapshot));

            stats.push_back(snapshot);
            if (stats.size() > 1000)
                stats.pop_front();
        }

    run_stats_plot(stats);

    if (ImGui::Begin("Debug Panel"))
    {
        ImGui::SeparatorText("GUI Stats");
        ImGui::Text("FPS: %.f (%0.3f ms)", io.Framerate, 1000.0f / io.Framerate);

        ImGui::SeparatorText("Timing & Frequency");
        ImGui::Text("Processing time: %.2f us", snapshot.processing_time_us);
        ImGui::Text("CFO:             %.2f Hz", snapshot.cfo);

        ImGui::SeparatorText("Sync Positions");
        ImGui::Text("CP Position:     %d", snapshot.cp_pos);
        ImGui::Text("ZC Position:     %d", snapshot.zc_pos);

        ImGui::SeparatorText("Status Flags");
        ImVec4 color_yes = ImVec4(0.0f, 1.0f, 0.4f, 1.0f);
        ImVec4 color_no = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

        ImGui::Text("CP Found:        ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.cp_found ? color_yes : color_no, snapshot.cp_found ? "YES" : "NO");

        ImGui::Text("ZC Found:        ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.zc_found ? color_yes : color_no, snapshot.zc_found ? "YES" : "NO");

        ImGui::Text("Is Packet:       ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.is_packet ? color_yes : color_no, snapshot.is_packet ? "YES" : "NO");

        ImGui::Text("Prev Packet Lost:");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.is_previous_packet_lost ? color_no : color_yes, snapshot.is_previous_packet_lost ? "YES" : "NO");
    }
    ImGui::End();
}

void App::run_stats_plot(const std::deque<StatsSnapshot> &stats)
{
    if (stats.empty())
        return;

    static std::vector<float> x_axis;
    static std::vector<float> proc_time_y;
    static std::vector<float> cfo_y;

    size_t size = stats.size();
    x_axis.resize(size);
    proc_time_y.resize(size);
    cfo_y.resize(size);

    for (size_t i = 0; i < size; ++i)
    {
        x_axis[i] = static_cast<float>(i);
        proc_time_y[i] = stats[i].processing_time_us;
        cfo_y[i] = stats[i].cfo;
    }

    if (ImGui::Begin("Signal Metrics"))
    {
        if (ImGui::BeginTabBar("MetricsTabBar"))
        {
            if (ImGui::BeginTabItem("Processing Time"))
            {
                if (ImPlot::BeginPlot("##ProcTime", ImVec2(-1, 250)))
                {
                    ImPlot::SetupAxes("Ticks", "Time (us)", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
                    ImPlot::PlotLine("Time", x_axis.data(), proc_time_y.data(), static_cast<int>(size));
                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("CFO Monitor"))
            {
                if (ImPlot::BeginPlot("##CFOMonitor", ImVec2(-1, 250)))
                {
                    ImPlot::SetupAxes("Ticks", "CFO (Hz)", ImPlotAxisFlags_None, ImPlotAxisFlags_AutoFit);
                    ImPlot::PlotLine("CFO", x_axis.data(), cfo_y.data(), static_cast<int>(size));
                    ImPlot::EndPlot();
                }
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void run_gui(Buffers &buf, std::vector<std::string> &sockets, socketData &sock)
{
    App app("SOIP", 1280, 720);

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
            app.begin_debug(buf);

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
    ImGui::SliderInt("Update Rate(ms)", &waterfall.update_interval_ms, 1, 100);

    static float min_db = -20.0f;
    static float max_db = 10.0f;

    ImGui::SliderFloat("Min dB", &min_db, -200.0f, 200.0f);
    ImGui::SliderFloat("Max dB", &max_db, -200.0f, 200.0f);

    static int colormap_idx = 1;
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

    if (samples.size() < fft_size)
        return;

    for (int i = 0; i < fft_size; ++i)
    {
        std::complex<float> windowed = samples[i] * window[i];
        fft_in[i][0] = windowed.real();
        fft_in[i][1] = windowed.imag();
    }
    fftwf_execute(fft_plan);

    const float norm = 1.0f / fft_size;
    const float norm_sq = norm * norm;
    const int half = fft_size / 2;

    for (int i = 0; i < fft_size; ++i)
    {
        int idx = (i + half) % fft_size;
        float re = fft_out[idx][0];
        float im = fft_out[idx][1];
        float db = 10.0f * std::log10f(norm_sq * (re * re + im * im) + 1e-12f);

        if (db > local_max_db[i])
        {
            local_max_db[i] = db;
        }
    }
    accumulated_blocks++;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_update);

    if (elapsed.count() < update_interval_ms)
        return;

    last_update = now;

    if (accumulated_blocks == 0)
        return;

    std::memmove(data.data() + fft_size, data.data(), (history_rows - 1) * fft_size * sizeof(float));
    std::memcpy(data.data(), local_max_db.data(), fft_size * sizeof(float));

    std::fill(local_max_db.begin(), local_max_db.end(), -200.0f);
    accumulated_blocks = 0;
}

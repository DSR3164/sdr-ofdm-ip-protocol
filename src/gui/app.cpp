#include "gui/app.hpp"
#include "gui/gui_layer.hpp"
#include "gui/phy_dev.hpp"
#include "gui/ip_dev.hpp"

#include <GL/glew.h>
#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"

App::App(const std::string &title, int width, int height)
{
    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER);
    window = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
    gl_context = SDL_GL_CreateContext(window);
    SDL_GL_SetSwapInterval(1);

    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO &io = ImGui::GetIO();
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

App::PlotSpec::PlotSpec(int stride_elements, ImPlotMarker marker, float marker_size)
{
    spec.Marker = marker;
    spec.MarkerSize = marker_size;
    spec.Stride = (int)sizeof(float) * stride_elements;
    spec.Offset = 0;
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

void App::control_wd()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Control Panel"))
        {
            ImGui::SeparatorText("Video Settings");
            ImGui::MenuItem("VSYNC", nullptr, &vsync_state);
                this->set_vsync_state(vsync_state);

            ImGui::SeparatorText("Workstation");

            ImGui::MenuItem("GUI Dev", nullptr, &gui_run);
            ImGui::MenuItem("PHY Dev", nullptr, &phy_run);
            ImGui::MenuItem("IP Dev", nullptr, &ip_run);

            ImGui::SeparatorText("Debug");

            ImGui::MenuItem("Open debug panel", nullptr, &debug_run);

            ImGui::EndMenu();
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

void App::begin_plot_1d(const std::string &label, std::span<const float> data)
{
    PlotSpec plot_1d(1);
    if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail())))
    {
        ImPlot::PlotLine(label.c_str(), data.data(), (int)data.size(), 1.0, 0.0, plot_1d.spec);
        ImPlot::EndPlot();
    }
}

void App::begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, std::span<const float> data)
{
    PlotSpec plot_2d(2);
    int count = data.size() / 2;

    if (ImPlot::BeginPlot(label.c_str(), ImGui::GetContentRegionAvail()))
    {
        ImPlot::PlotLine(label_i.c_str(), data.data(), count, 1.0, 0.0, plot_2d.spec);
        ImPlot::PlotLine(label_q.c_str(), data.data() + 1, count, 1.0, 0.0, plot_2d.spec);
        ImPlot::EndPlot();
    }
}

template<typename T>
void App::begin_scatter(const std::string &label, std::span<const T> data)
{
    PlotSpec plot_scatter(2, ImPlotMarker_Asterisk, 1.0f);
    int count = data.size() / 2;

    if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail()), ImPlotFlags_Equal))
    {
        ImPlot::PlotScatter(label.c_str(), data.data(), data.data() + 1, count, plot_scatter.spec);
        ImPlot::EndPlot();
    }
}

void run_gui(Buffers &sd)
{
    App app("Development", 1280, 720);

    while (app.is_open())
    {
        app.start_frame();
        app.control_wd();

        if (app.is_debug_run())
            app.begin_debug();

        if (app.is_gui_run())
            gui_dev(app);

        if (app.is_phy_run())
            phy_dev(app);

        if (app.is_ip_run())
            ip_dev(app);

        app.stop_frame();
    }
}
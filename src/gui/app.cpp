#include "gui/app.hpp"
#include "gui/gui_layer.hpp"
#include "gui/phy_dev.hpp"
#include "gui/ip_dev.hpp"

#include <GL/glew.h>
#include "implot.h"
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
            ImGui::SeparatorText("Workstation");

            ImGui::MenuItem("GUI Dev", nullptr, &gui_run);
            ImGui::MenuItem("PHY Dev", nullptr, &phy_run);
            ImGui::MenuItem("IP Dev", nullptr, &ip_run);

            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void App::begin_plot_1d(const std::string &label, const float *data, size_t data_size)
{
    if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail())))
    {
        ImPlot::PlotLine(label.c_str(), data, data_size);
        ImPlot::EndPlot();
    }
}

void App::begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, const float *data, size_t data_size)
{
    int count = (int)(data_size / 2);
    auto get_i = [](int i, void *d) { return ImPlotPoint(i, ((float *)d)[i * 2]); };
    auto get_q = [](int i, void *d) { return ImPlotPoint(i, ((float *)d)[i * 2 + 1]); };

    if (ImPlot::BeginPlot(label.c_str(), ImGui::GetContentRegionAvail()))
    {
        ImPlot::PlotLineG(label_i.c_str(), get_i, (void *)data, count);
        ImPlot::PlotLineG(label_q.c_str(), get_q, (void *)data, count);
        ImPlot::EndPlot();
    }
}

void App::begin_scatter(const std::string &label, const float *data, size_t data_size)
{
    int count = data_size / 2;
    auto get_iq = [](int i, void *d)
        {
            float *f_data = (float *)d;
            return ImPlotPoint(f_data[i * 2], f_data[i * 2 + 1]);
        };

    if (ImPlot::BeginPlot(label.c_str(), ImVec2(ImGui::GetContentRegionAvail()), ImPlotFlags_Equal))
    {
        ImPlotSpec spec;
        spec.Marker = ImPlotMarker_Square;
        spec.MarkerSize = 2.0f;
        ImPlot::PlotScatterG(label.c_str(), get_iq, (void *)data, count, spec);
        ImPlot::EndPlot();
    }
}

void run_gui(SharedData &sd)
{
    App app("Development", 1280, 720);

    while (app.is_open())
    {
        app.start_frame();
        app.control_wd();

        if (app.is_gui_run())
            gui_dev(app);

        if (app.is_phy_run())
            phy_dev(app);

        if (app.is_ip_run())
            ip_dev(app);

        app.stop_frame();
    }
}
#include "gui/app.hpp"
#include "gui/gui_layer.hpp"
#include "gui/ip_dev.hpp"
#include "gui/phy_dev.hpp"
#include "sockets.hpp"

#include "backends/imgui_impl_opengl3.h"
#include "backends/imgui_impl_sdl2.h"
#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <GL/glew.h>
#include <filesystem>
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
    ImPlot3D::CreateContext();

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
    ImPlot3D::DestroyContext();
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

void App::control_wd(std::vector<std::string> &sockets)
{
    bool chosen_socket = false;

    if (ImGui::BeginMainMenuBar())
    {
        bool menu_open = ImGui::BeginMenu("Control Panel");

        if (menu_open)
        {
            if (!control_menu_was_open)
                found_sockets(sockets);

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

void App::begin_debug(Buffers &buf)
{
    ImGuiIO &io = ImGui::GetIO();
    static Stats s_last{};
    std::vector<Stats> s_vec;

    buf.stats.read(s_vec);
    if (!s_vec.empty())
        s_last = s_vec.back();

    if (ImGui::Begin("Debug Panel"))
    {
        ImGui::SeparatorText("Statistics");
        ImGui::Text("FPS: %.f (%0.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        ImGui::Text("ZC Not Found: %u", s_last.zc_not_found);
        ImGui::Text("CP Not Found: %u", s_last.cp_not_found);
        ImGui::Text("CFO Jumped: %u", s_last.cfo_jumped);
        ImGui::Text("Packet Found: %u", s_last.packet_found);
        ImGui::Text("Packet Lost: %u", s_last.packet_lost);
        ImGui::Text("Packet Loss: %.2f%%", s_last.packet_loss);
        ImGui::Text("Mean Time: %.2f us", s_last.mean_time_us);
    }
    ImGui::End();
}

void run_gui(Buffers &buf, std::vector<std::string> &sockets, socketData &sock)
{
    App app("Development", 1280, 720);

    while (app.is_open())
    {
        app.start_frame();
        app.control_wd(sockets);

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

    return socket;
}

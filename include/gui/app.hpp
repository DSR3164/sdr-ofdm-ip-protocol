#pragma once

#include "common.hpp"

#include <SDL2/SDL.h>
#include <string>
#include <span>

class App {
public:
    App(const std::string &title, int width, int height);
    ~App();

    bool is_open() { return running; }
    bool is_gui_run() { return gui_run; }
    bool is_phy_run() { return phy_run; }
    bool is_ip_run() { return ip_run; }
    void start_frame();
    void stop_frame();
    void control_wd();
    void begin_plot_1d(const std::string &label, std::span<const float> data);
    void begin_plot_2d(const std::string &label, const std::string &label_i, const std::string &label_q, std::span<const float> data);
    void begin_scatter(const std::string &label, std::span<const float> data);
    
private:
    SDL_Window *window;
    SDL_GLContext gl_context;
    bool running = true;
    bool gui_run = false;
    bool phy_run = false;
    bool ip_run = false;
};

void run_gui(SharedData &sd);
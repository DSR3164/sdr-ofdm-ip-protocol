#include "common.hpp"
#include "gui/app.hpp"

#include <thread>

float lat = 1.0f;

int main()
{
    Buffers bufs(1920 * 2);
    std::thread gui(run_gui, std::ref(bufs));
    gui.join();
    return 0;
}
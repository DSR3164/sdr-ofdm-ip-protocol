#include "common.hpp"
#include "gui/app.hpp"

#include <thread>

int main()
{
    SharedData sd;
    std::thread gui(run_gui, std::ref(sd));
    gui.join();

    return 0;
}
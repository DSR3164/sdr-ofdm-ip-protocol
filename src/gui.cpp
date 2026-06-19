#include "gui/app.hpp"
#include "bridges.hpp"

#include <functional>
#include <string>
#include <thread>
#include <vector>

extern std::atomic_bool quit;

int main()
{
    std::vector<std::string> all_sockets;
    found_sockets(all_sockets);
    Buffers bufs(1920 * 2);
    socketData sock(false);

    if (getuid() != 0)
    {
        logs::main.critical("Please run with sudo or as root");
        return 0;
    }

    {
        ThreadJoiner control{ "control", std::jthread(run_control_bridge_server, std::ref(sock)), "gui" };
        ThreadJoiner gui{ "gui", std::jthread(run_gui, std::ref(bufs), std::ref(all_sockets), std::ref(sock), std::ref(quit)), "gui" };
        ThreadJoiner dsp_bridge{ "dsp_bridge", std::jthread(run_dsp_bridge, std::ref(bufs), std::ref(sock)), "gui" };
        ThreadJoiner ip_brigde{ "ip_brigde", std::jthread(run_ip_brigde, std::ref(bufs), std::ref(sock)), "gui" };
        ThreadJoiner stats_bridge{ "stats_bridge", std::jthread(run_stats_bridge, std::ref(bufs), std::ref(sock)), "gui" };

        while (!quit)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    logs::gui.info("All threads joined. Exiting.");
    return 0;
}

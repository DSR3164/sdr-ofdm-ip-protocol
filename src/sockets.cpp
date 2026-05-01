#include "sockets.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <zmq.hpp>

socketData::socketData(const bool setup_dir, const std::string &base_folder) : is_owner(setup_dir)
{
    namespace fs = std::filesystem;

    if (setup_dir && is_owner)
    {
        socketPath = setup_socket_dir(base_folder);
        fs::path base = socketPath;

        ip_socket = "ipc://" + (base / "ip_gui.sock").string();
        phy_socket = "ipc://" + (base / "dsp_gui.sock").string();
    }
    else
    {
        ip_socket = "";
        phy_socket = "";
    }
}

socketData::~socketData()
{
    logs::socket.info("Socket destructor called");
    namespace fs = std::filesystem;

    if (is_owner && !socketPath.empty())
    {
        try
        {
            if (fs::exists(socketPath))
            {
                fs::remove_all(socketPath);
                logs::socket.info("Socket directory cleaned up: {}", socketPath);
            }
            else
            {
                logs::socket.error("Socket directory not found: {}", socketPath);
            }
        }
        catch (const fs::filesystem_error &e)
        {
            logs::main.error("Cleanup error: {}", e.what());
        }
    }
    else
    {
        logs::socket.info("Not owner or empty path [{}], skipping cleanup", socketPath);
    }
}

std::string socketData::setup_socket_dir(const std::string &folder_name)
{
    namespace fs = std::filesystem;
    socketPid = std::to_string(getpid());
    std::string uniq_folder = folder_name + "_" + socketPid;
    fs::path p = fs::temp_directory_path() / uniq_folder;

    try
    {
        fs::create_directories(p);
        fs::permissions(p, fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all, fs::perm_options::replace);
        logs::main.info("Socket directory ready: {}", p.string());

        if (p.string().length() > 100)
            logs::main.warn("Path is too long for Unix Sockets! (> 100 chars)");
    }
    catch (const fs::filesystem_error &e)
    {
        logs::main.error("Filesystem library error {}", e.what());
    }

    return p.string();
}

void found_sockets(std::vector<std::string> &sockets, const std::string base_name)
{
    namespace fs = std::filesystem;
    sockets.clear();
    sockets.reserve(100);
    const std::string tmp_path = "/tmp";
    static std::atomic<bool> searching = false;

    if (!searching.exchange(true))
    {
        try
        {
            if (fs::exists(tmp_path) && fs::is_directory(tmp_path))
            {
                for (const auto &entry : fs::directory_iterator(tmp_path))
                {
                    std::string filename = entry.path().filename().string();

                    if (filename.find(base_name) == 0)
                    {
                        logs::gui.info("Found socket directory: {}", filename);
                        sockets.push_back(filename);

                        if (fs::is_directory(entry))
                        {
                            for (const auto &sock : fs::directory_iterator(entry))
                            {
                                logs::gui.info("  -> Socket file: {}", sock.path().filename().string());
                            }
                        }
                    }
                }
            }
        }
        catch (const fs::filesystem_error &e)
        {
            logs::gui.error("FS error: {}", e.what());
        }
        searching.store(false);
    }
}

bool IPC::start_server(const std::string &path)
{
    try
    {
        _socket.set(zmq::sockopt::linger, 0);
        _socket.set(zmq::sockopt::sndhwm, 10);
        _socket.bind(path);
        return true;
    }
    catch (const zmq::error_t &e)
    {
        logs::socket.error("Failed to bind to {}: {}", path, e.what());
        return false;
    }
}

bool IPC::connect_to(const std::string &path)
{
    try
    {
        _socket = zmq::socket_t(_context, zmq::socket_type::sub);
        _socket.set(zmq::sockopt::subscribe, "");
        _socket.set(zmq::sockopt::rcvtimeo, 500);
        _socket.connect(path);

        logs::socket.info("SUB socket connected to {}", path);
        return true;
    }
    catch (const zmq::error_t &e)
    {
        logs::socket.error("Failed to connect to {}: {}", path, e.what());
        return false;
    }
}

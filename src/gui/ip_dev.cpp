#include "gui/ip_dev.hpp"
#include "spdlog/spdlog.h"

#include <vector>
#include "implot.h"

void ip_dev(App &app) // IP layer
{
    (void)app;
    static IPC client;
    static ipc_header header;
    static bool init = false;
    static ImPlotSpec specs;
    static std::vector<uint8_t> bytes(100);

    if (!init)
    {
        if (client.connect_to_socket("/tmp/ip_gui.sock") == 0)
        {
            init = true;
            spdlog::info("IP device GUI initialized");
        }
        else
            return;
    }

    if (!client.recv_frame(header, bytes))
        return;

    const uint8_t *raw_ptr_bits = reinterpret_cast<const uint8_t *>(bytes.data());

    if (ImGui::Begin("IP Layer"))
    {
        ImGui::Text("[IP] Ver=%d HLen=%d TOS=%02X Len=%d Proto=%d Src=%d.%d.%d.%d Dest=%d.%d.%d.%d",
                    (raw_ptr_bits[0] >> 4) & 0xF,
                    (raw_ptr_bits[0] & 0xF) * 4,
                    raw_ptr_bits[1],
                    (raw_ptr_bits[2] << 8) | raw_ptr_bits[3],
                    raw_ptr_bits[9],
                    raw_ptr_bits[12], raw_ptr_bits[13],
                    raw_ptr_bits[14], raw_ptr_bits[15],
                    raw_ptr_bits[16], raw_ptr_bits[17],
                    raw_ptr_bits[18], raw_ptr_bits[19]);

        if (ImPlot::BeginPlot("IP Bits", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxes("Index", "Value");
            ImPlot::PlotStairs("Bits (int16)", raw_ptr_bits, bytes.size());
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

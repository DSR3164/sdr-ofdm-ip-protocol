#include "logger.hpp"
#include "gui/ip_dev.hpp"

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
            logs::gui.info("IP device GUI initialized");
        }
        else
            return;
    }

    if (!client.recv_frame(header, bytes))
        return;

    const uint8_t *raw_ptr_bits = reinterpret_cast<const uint8_t *>(bytes.data());

    if (ImGui::Begin("IP Layer"))
    {
        ImGui::Text("[Hdr] Magic=0x%04X PLen=%d Seq=%d Flags=%d Reserved=%d",
            (raw_ptr_bits[0] << 8) | raw_ptr_bits[1],
            (raw_ptr_bits[2] << 8) | raw_ptr_bits[3],
            (raw_ptr_bits[4] << 8) | raw_ptr_bits[5],
            raw_ptr_bits[6], raw_ptr_bits[7]);
        ImGui::Text("[IP] Ver=%d HLen=%d TOS=%02X Len=%d Proto=%d Src=%d.%d.%d.%d Dest=%d.%d.%d.%d",
            (raw_ptr_bits[8] >> 4) & 0xF,
            (raw_ptr_bits[8] & 0xF) * 4,
            raw_ptr_bits[9],
            (raw_ptr_bits[10] << 8) | raw_ptr_bits[3],
            raw_ptr_bits[17],
            raw_ptr_bits[20], raw_ptr_bits[21],
            raw_ptr_bits[22], raw_ptr_bits[23],
            raw_ptr_bits[24], raw_ptr_bits[25],
            raw_ptr_bits[26], raw_ptr_bits[27]);

        if (ImPlot::BeginPlot("IP Bits", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxes("Index", "Value");
            ImPlot::PlotStairs("Bits (int16)", raw_ptr_bits, bytes.size());
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

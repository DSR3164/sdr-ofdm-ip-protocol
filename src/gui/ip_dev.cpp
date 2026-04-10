#include "gui/ip_dev.hpp"

#include <imgui.h>
#include <implot.h>
#include <vector>

void ip_dev(App &app, Buffers &data) // IP layer
{
    (void)app;
    static ImPlotSpec specs;
    static std::vector<uint8_t> bytes(10, 0);

    data.ip.read(bytes);

    const uint8_t *raw_ptr_bits = reinterpret_cast<const uint8_t *>(bytes.data());

    if (ImGui::Begin("IP Layer"))
    {
        // clang-format off
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

        // clang-format on
        if (ImPlot::BeginPlot("IP Bits", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxes("Index", "Value");
            ImPlot::PlotStairs("Bits (int16)", raw_ptr_bits, bytes.size());
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

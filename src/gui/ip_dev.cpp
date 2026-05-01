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

    const uint8_t *raw_ptr_bytes = reinterpret_cast<const uint8_t *>(bytes.data());

    const char *flag = "None";
    uint8_t flag_byte = raw_ptr_bytes[7];

    if ((flag_byte & 0x01) && (flag_byte & 0x02))
        flag = "First n Last";
    else if (flag_byte & 0x01)
        flag = "Last";
    else if (flag_byte & 0x02)
        flag = "First";

    if (ImGui::Begin("IP Layer"))
    {
        // clang-format off
        ImGui::Text("[Hdr] Magic=0x%04X PLen=%d Seq=%d Id=%d Flags=%s",
            (raw_ptr_bytes[0] << 8) | raw_ptr_bytes[1],
            (raw_ptr_bytes[2] << 8) | raw_ptr_bytes[3],
            (raw_ptr_bytes[4] << 8) | raw_ptr_bytes[5],
            (raw_ptr_bytes[5] << 8) | raw_ptr_bytes[6],
            flag);
        ImGui::Text("[IP] Ver=%d HLen=%d TOS=%02X Len=%d Proto=%d Src=%d.%d.%d.%d Dest=%d.%d.%d.%d",
            (raw_ptr_bytes[8] >> 4) & 0xF,
            (raw_ptr_bytes[8] & 0xF) * 4,
            raw_ptr_bytes[9],
            (raw_ptr_bytes[10] << 8) | raw_ptr_bytes[3],
            raw_ptr_bytes[17],
            raw_ptr_bytes[20], raw_ptr_bytes[21],
            raw_ptr_bytes[22], raw_ptr_bytes[23],
            raw_ptr_bytes[24], raw_ptr_bytes[25],
            raw_ptr_bytes[26], raw_ptr_bytes[27]);

        // clang-format on
        if (ImPlot::BeginPlot("IP Bits", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxes("Index", "Value");
            ImPlot::PlotStairs("Bits (int16)", raw_ptr_bytes, bytes.size());
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

#include "ip/ip_layer.hpp"
#include "gui/ip_dev.hpp"

#include <imgui.h>
#include <implot.h>
#include <vector>

void ip_dev(App &app, Buffers &data) // IP layer
{
    (void)app;
    static ImPlotSpec specs;
    std::vector<uint8_t> bytes(1500);

    data.ip.read(bytes);

    const uint8_t *raw_ptr_bytes = reinterpret_cast<const uint8_t *>(bytes.data());

    const char *flag = "None";
    uint8_t flag_byte = raw_ptr_bytes[8];

    if ((flag_byte & 0x01) && (flag_byte & 0x02))
        flag = "First n Last";
    else if (flag_byte & 0x01)
        flag = "Last";
    else if (flag_byte & 0x02)
        flag = "First";

    if (ImGui::Begin("IP Layer"))
    {
        if (bytes.size() < sizeof(FrameHeader) + 20)
        {
            ImGui::Text("Packet too short");
            ImGui::End();
            return;
        }
        // clang-format off
        ImGui::Text("[Hdr] Magic=0x%04X PLen=%d Seq=%d Id=%d Flags=%s",
            (raw_ptr_bytes[0] << 8) | raw_ptr_bytes[1],
            (raw_ptr_bytes[2] << 8) | raw_ptr_bytes[3],
            (raw_ptr_bytes[4] << 8) | raw_ptr_bytes[5],
            (raw_ptr_bytes[6] << 8) | raw_ptr_bytes[7],
            flag
        );
        
        const uint8_t* ip = raw_ptr_bytes + sizeof(FrameHeader);
        ImGui::Text(
            "[IP] Ver=%d HLen=%d TOS=%02X Len=%d Proto=%d Src=%d.%d.%d.%d Dest=%d.%d.%d.%d",
            (ip[0] >> 4) & 0xF,
            (ip[0] & 0xF) * 4,
            ip[1],
            (ip[2] << 8) | ip[3],
            ip[9],
            ip[12], ip[13], ip[14], ip[15],
            ip[16], ip[17], ip[18], ip[19]
        );
        // clang-format on
    }
    ImGui::End();
}

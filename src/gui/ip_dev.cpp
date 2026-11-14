#include "ip/ip_layer.hpp"
#include "gui/ip_dev.hpp"

#include <arpa/inet.h>
#include <imgui.h>
#include <implot.h>
#include <vector>

void ip_dev(App &app, Buffers &data) // IP layer
{
    (void)app;
    static ImPlotSpec specs;
    struct GuiClient {
        uint16_t rnti;
        uint32_t user_token;
        uint32_t ip;
    };

    static std::vector<GuiClient> clients;

    std::vector<uint8_t> bytes;
    std::vector<uint8_t> users_bytes;

    bool has_ip_packet = (data.ip.read(bytes) == 0);
    bool has_users_update = (data.users.read(users_bytes) == 0);

    if (has_users_update && users_bytes.size() >= 4)
    {
        auto read_u16 = [](const uint8_t *ptr) -> uint16_t
        {
            return (static_cast<uint16_t>(ptr[0]) << 8) | static_cast<uint16_t>(ptr[1]);
        };

        auto read_u32 = [](const uint8_t *ptr) -> uint32_t
        {
            return (static_cast<uint32_t>(ptr[0]) << 24) | (static_cast<uint32_t>(ptr[1]) << 16) | (static_cast<uint32_t>(ptr[2]) << 8) | static_cast<uint32_t>(ptr[3]);
        };

        uint32_t count = read_u32(users_bytes.data());

        if (count <= (users_bytes.size() - 4) / 10 && users_bytes.size() == 4 + count * 10)
        {
            std::vector<GuiClient> updated_clients;
            updated_clients.reserve(count);

            size_t offset = 4;

            for (uint32_t i = 0; i < count; ++i)
            {
                GuiClient client{};

                client.rnti = read_u16(users_bytes.data() + offset);
                offset += 2;

                client.user_token = read_u32(users_bytes.data() + offset);
                offset += 4;

                client.ip = read_u32(users_bytes.data() + offset);
                offset += 4;

                updated_clients.push_back(client);
            }

            clients = std::move(updated_clients);
        }
    }

    if (ImGui::Begin("IP Layer"))
    {
        // IP packet information
        if (has_ip_packet)
        {
            if (bytes.size() < sizeof(FrameHeader) + 20)
            {
                ImGui::Text("Packet too short или поврежден");
            }
            else
            {
                const uint8_t *raw_ptr_bytes = bytes.data();

                const char *flag = "None";
                uint8_t flag_byte = raw_ptr_bytes[8];

                if ((flag_byte & 0x01) && (flag_byte & 0x02))
                    flag = "First and Last";
                else if (flag_byte & 0x01)
                    flag = "Last";
                else if (flag_byte & 0x02)
                    flag = "First";

                // clang-format off
                ImGui::Text(
                    "[Hdr] Magic=0x%04X PLen=%d Seq=%d Id=%d Flags=%s",
                    (raw_ptr_bytes[0] << 8) | raw_ptr_bytes[1],
                    (raw_ptr_bytes[2] << 8) | raw_ptr_bytes[3],
                    (raw_ptr_bytes[4] << 8) | raw_ptr_bytes[5],
                    (raw_ptr_bytes[6] << 8) | raw_ptr_bytes[7],
                    flag
                );

                const uint8_t *ip = raw_ptr_bytes + sizeof(FrameHeader);

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
        }
        else
        {
            ImGui::Text("Waiting for IP packets...");
        }

        // Connected users
        ImGui::Separator();

        ImGui::Text("Connected users: %zu", clients.size());

        if (ImGui::BeginTable(
                "UsersTable",
                3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            ))
        {
            ImGui::TableSetupColumn("RNTI");
            ImGui::TableSetupColumn("IP address");
            ImGui::TableSetupColumn("Token");

            ImGui::TableHeadersRow();

            for (const auto &client : clients)
            {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%u", client.rnti);

                ImGui::TableSetColumnIndex(1);
                ImGui::Text(
                    "%u.%u.%u.%u",
                    (client.ip >> 24) & 0xFF,
                    (client.ip >> 16) & 0xFF,
                    (client.ip >> 8) & 0xFF,
                    client.ip & 0xFF
                );

                ImGui::TableSetColumnIndex(2);
                ImGui::Text("0x%08X", client.user_token);
            }

            ImGui::EndTable();
        }
    }

    ImGui::End();
}

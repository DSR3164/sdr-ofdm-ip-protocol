#include "common.hpp"
#include "gui/phy_dev.hpp"

#include <implot.h>
#include <vector>

extern float lat;

void phy_dev(App &app, Buffers &data) // Phy layer
{
    static WaterfallData waterfall(1920, 100);
    static std::vector<std::complex<float>> symbols(1920);
    static std::vector<std::complex<float>> raw(3840);
    static ImGuiIO &io = ImGui::GetIO();
    static ImPlotSpec specs;
    static ImPlotSpec specs2;
    static std::string label = "Raw signal";
    static bool init = false;
    if (!init)
    {
        specs.Stride = sizeof(float);
        specs.Offset = 0;
        specs2.Stride = sizeof(float) * 2;
        specs2.Offset = 0;
        specs.Marker = ImPlotMarker_Asterisk;
        specs.MarkerSize = 2.0f;
        init = true;
    }

    data.dsp.read(symbols);
    data.sdr_raw.read(raw);
    const float *raw_ptr = reinterpret_cast<const float *>(symbols.data());

    if (ImGui::Begin("Constellation"))
    {
        ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        app.begin_scatter<float, std::complex<float>>(label, symbols);
    }
    ImGui::End();
    if (ImGui::Begin("Time domain raw"))
        app.begin_plot_2d<float, std::complex<float>>(label, "I", "Q", raw);
    ImGui::End();

    // Stats
    static StatsSnapshot snapshot{};
    std::vector<uint8_t> raw_stats;

    if (data.stats.read(raw_stats) == 0)
        if (raw_stats.size() == sizeof(StatsSnapshot))
            std::memcpy(&snapshot, raw_stats.data(), sizeof(StatsSnapshot));

    if (ImGui::Begin("PHY Stats"))
    {
        ImGui::SeparatorText("Timing & Frequency");
        ImGui::Text("Processing time: %.2f us", snapshot.processing_time_us);
        ImGui::Text("CFO:             %.2f Hz", snapshot.cfo);

        ImGui::SeparatorText("Sync Positions");
        ImGui::Text("CP Position:     %d", snapshot.cp_pos);
        ImGui::Text("ZC Position:     %d", snapshot.zc_pos);

        ImGui::SeparatorText("Status Flags");
        ImVec4 color_yes = ImVec4(0.0f, 1.0f, 0.4f, 1.0f);
        ImVec4 color_no = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);

        ImGui::Text("CP Found:        ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.cp_found ? color_yes : color_no, snapshot.cp_found ? "YES" : "NO");

        ImGui::Text("ZC Found:        ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.zc_found ? color_yes : color_no, snapshot.zc_found ? "YES" : "NO");

        ImGui::Text("Is Packet:       ");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.is_packet ? color_yes : color_no, snapshot.is_packet ? "YES" : "NO");

        ImGui::Text("Prev Packet Lost:");
        ImGui::SameLine();
        ImGui::TextColored(snapshot.is_previous_packet_lost ? color_no : color_yes, snapshot.is_previous_packet_lost ? "YES" : "NO");
    }
    ImGui::End();

    if (ImGui::Begin("Waterfall"))
        app.run_waterfall("##Waterfall", waterfall, raw);
    ImGui::End();
}

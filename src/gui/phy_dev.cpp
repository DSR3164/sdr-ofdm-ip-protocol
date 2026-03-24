#include "gui/phy_dev.hpp"

#include <vector>
#include "implot.h"
#include <thread>

void phy_dev(App &app) // Phy layer
{
    (void)app;
    static std::vector<int16_t> buffer;
    static IPC client;
    static ipc_header h;
    static std::string label = "Raw signal";
    static bool init = false;
    static ImGuiIO &io = ImGui::GetIO();
    static ImPlotSpec specs;
    static ImPlotSpec specs2;
    static float lat = 1.0f;

    if (!init)
    {
        while (client.connect_to_socket("/tmp/dsp_gui.sock") == 1)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        init = true;
        specs.Stride = sizeof(int16_t); // Шаг в байтах между элементами
        specs.Offset = 0;
        specs2.Stride = sizeof(int16_t); // Шаг в байтах между элементами
        specs2.Offset = 0;
        specs.Marker = ImPlotMarker_Square;
        specs.MarkerSize = 2.0f;
    }

    client.recv_frame(h, buffer);

    static int size = buffer.size() / 2;
    const int16_t *raw_ptr = reinterpret_cast<const int16_t *>(buffer.data());

    lat = 0.001 * (client.now_ns() - h.timestamp_ns) + 0.999 * lat;

    if (ImGui::Begin("Phy"))
    {
        ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        if (ImPlot::BeginPlot("Scatter", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Once);
            ImPlot::PlotScatter("Const", raw_ptr, raw_ptr + 1, size, specs);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
    if (ImGui::Begin("Line"))
    {
        if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
        {
            ImPlot::PlotLine("I", raw_ptr, size, 1.0, 0.0, specs2);
            ImPlot::PlotLine("Q", raw_ptr + 1, size, 1.0, 0.0, specs2);
        }
        ImPlot::EndPlot();
    }
    ImGui::End();
}
#include "gui/phy_dev.hpp"

#include <vector>
#include "implot.h"
#include <thread>

extern float lat;

void phy_dev(App &app, Buffers &data) // Phy layer
{
    static std::vector<std::complex<float>> buffer(1920);
    static ImGuiIO &io = ImGui::GetIO();
    static ImPlotSpec specs;
    static ImPlotSpec specs2;
    static std::string label = "Raw signal";
    specs.Stride = sizeof(float);
        specs.Offset = 0;
    specs2.Stride = sizeof(float) * 2;
        specs2.Offset = 0;
    specs.Marker = ImPlotMarker_Asterisk;
        specs.MarkerSize = 2.0f;

    data.dsp.read(buffer);
    auto size = buffer.size() / 2;
    const float *raw_ptr = reinterpret_cast<const float *>(buffer.data());

    if (ImGui::Begin("Phy"))
    {
        ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        if (ImPlot::BeginPlot("Scatter", ImGui::GetContentRegionAvail()))
        {
            ImPlot::SetupAxesLimits(-2048, 2048, -2048, 2048, ImPlotCond_Once);
            ImPlot::PlotScatter("Const", raw_ptr, raw_ptr + 1, size, specs);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
    if (ImGui::Begin("Line"))
    {
        if (ImPlot::BeginPlot("I:Q", ImGui::GetContentRegionAvail()))
        {
            ImPlot::PlotLine("I", raw_ptr, size, 1.0, 0.0, specs2);
            ImPlot::PlotLine("Q", raw_ptr + 1, size, 1.0, 0.0, specs2);
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
}

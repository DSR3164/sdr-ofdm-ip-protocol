#include "gui/phy_dev.hpp"
#include "imgui.h"
#include "implot.h"

#include <vector>

extern float lat;

void phy_dev(App &app, Buffers &data) // Phy layer
{
    static WaterfallData waterfall(3840, 125);
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

    if (ImGui::Begin("Waterfall"))
        app.run_waterfall("##Waterfall", waterfall, raw);
    ImGui::End();

    if (ImGui::Begin("Constellation"))
    {
        ImPlot::SetNextAxesLimits(-1.5f, 1.3f, -1.3f, 1.3f);
        ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        app.begin_scatter<float, std::complex<float>>(label, symbols);
    }
    ImGui::End();
    if (ImGui::Begin("Time domain raw"))
    {
        ImPlot::SetNextAxesLimits(0.0f, 1920.0f * 2.0f, -1000.0f, 1000.0f);
        app.begin_plot_2d<float, std::complex<float>>(label, "I", "Q", raw);
    }
    ImGui::End();
}

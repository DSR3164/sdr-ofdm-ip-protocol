#include "gui/phy_dev.hpp"

#include <implot.h>
#include <vector>

extern float lat;

void phy_dev(App &app, Buffers &data) // Phy layer
{
    static std::vector<std::complex<float>> buffer(1920);
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

    data.dsp.read(buffer);
    auto size = buffer.size() / 2;
    const float *raw_ptr = reinterpret_cast<const float *>(buffer.data());

    if (ImGui::Begin("Phy"))
    {
        ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
        ImGui::Text("FPS: %.1f (%.3f ms)", io.Framerate, 1000.0f / io.Framerate);
        app.begin_scatter<float, std::complex<float>>(label, buffer);
    }
    ImGui::End();
    if (ImGui::Begin("Line"))
        app.begin_plot_2d<float, std::complex<float>>(label, "I", "Q", buffer);
    ImGui::End();
}

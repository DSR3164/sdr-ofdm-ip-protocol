#include "gui/phy_dev.hpp"
#include "imgui.h"

#include <vector>

extern float lat;

void phy_dev(App &app, Buffers &data) {
    static std::vector<std::complex<float>> buffer;

    data.dsp.read(buffer);

    if (ImGui::Begin("Phy"))
    {
        if (!buffer.empty())
        {
            ImGui::Text("Latency  %.2f mcs | %.2f ms", lat / 1e3, lat / 1e6);
            ImGui::SameLine();
            ImGui::Text("Buffer size: %zu", buffer.size());
            app.begin_scatter<float, std::complex<float>>("Raw I/Q", buffer);
        }
        else
            ImGui::Text("Waiting for data...");
    }
    ImGui::End();

    if (ImGui::Begin("Line"))
    {
        if (!buffer.empty())
            app.begin_plot_2d<float, std::complex<float>>("Raw samples", "I", "Q", buffer);
        else
            ImGui::Text("Waiting for data...");
    }
    ImGui::End();
}

#include "gui/gui_layer.hpp"
#include "imgui.h"
#include "implot3d.h"
#include <cmath>
#include <cstddef>
#include <numbers>

void gui_dev(App &app) // GUI layer
{
    static float size[3] = {1.0f, 1.0f, 1.0f};
    ImPlot3DStyle& style = ImPlot3D::GetStyle();

    std::vector<float> x(1000);
    std::vector<float> y(1000);
    std::vector<float> z(1000);
    std::vector<float> t(1000);

    for (size_t i = 0; i < t.size(); ++i)
        t[i] = i + 1;

    const float fs = 1000.0f;
    const float freq = 100.0f;

    for (size_t i = 0; i < x.size(); ++i)
    {
        float t_val = i / fs;
        float signal = 2.0f * std::numbers::pi * freq * t_val;

        x[i] = std::cos(signal);
        y[i] = std::sin(signal);
        z[i] = t_val;
    }

    if (ImGui::Begin("Test"))
    {
        ImGui::SliderFloat2("PlotDefaultSize", (float*)&style.PlotDefaultSize, 0.0f, 1000, "%.0f");
        ImGui::SliderFloat2("PlotMinSize", (float*)&style.PlotMinSize, 0.0f, 300, "%.0f");
        ImGui::SliderFloat2("PlotPadding", (float*)&style.PlotPadding, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat3("Scale", size, 0.1f, 10.0f);
        if (ImPlot3D::BeginPlot("First 3D"))
        {
            ImPlot3D::SetupBoxScale(size[0], size[1], size[2]);
            ImPlot3D::PlotLine("3D Plot", x.data(), y.data(), z.data(), x.size());
            ImPlot3D::EndPlot();
        }
    }
    ImGui::End();

    ImPlot3D::ShowDemoWindow();
}

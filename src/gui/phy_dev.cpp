#include "gui/phy_dev.hpp"

#include "imgui.h"
#include "implot.h"
#include "implot3d.h"
#include <cstddef>
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

    struct Point3D { float x, y, z; };
    static std::vector<Point3D> datas;
    ImPlot3DStyle& style = ImPlot3D::GetStyle();
    static float sizes[3] = {1.0f, 1.0f, 1.0f};

    ImPlot3DSpec spec;
    spec.Stride = sizeof(Point3D);
    spec.Marker = ImPlotMarker_Circle;
    spec.MarkerSize = 1.0f;

    size_t count = buffer.size();
    if (datas.size() != count)
        datas.resize(count);


    for (size_t i = 0; i < count; ++i) {
        datas[i].x = buffer[i].real();
        datas[i].y = buffer[i].imag();
        datas[i].z = (float)i;
    }

    if (ImGui::Begin("3d")) {
        ImGui::SliderFloat2("PlotDefaultSize", (float*)&style.PlotDefaultSize, 0.0f, 1000, "%.0f");
        ImGui::SliderFloat2("PlotMinSize", (float*)&style.PlotMinSize, 0.0f, 300, "%.0f");
        ImGui::SliderFloat2("PlotPadding", (float*)&style.PlotPadding, 0.0f, 20.0f, "%.0f");
        ImGui::SliderFloat3("Scale", sizes, 0.1f, 10.0f);
        ImGui::Text("REAL: %f", buffer[10].real());
        ImGui::Text("IMAG: %f", buffer[10].imag());
        if (ImPlot3D::BeginPlot("Signals", ImVec2(-1, -1))) {
            ImPlot3D::SetupAxes("I", "Q", "Time");
            ImPlot3D::SetupBoxScale(sizes[0], sizes[1], sizes[2]);
            ImPlot3D::PlotLine("Signal", &datas[0].x, &datas[0].y, &datas[0].z, count, spec);
            ImPlot3D::EndPlot();
        }
    }
    ImGui::End();
}

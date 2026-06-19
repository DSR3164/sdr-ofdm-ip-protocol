#include "common.hpp"
#include "gui/phy_dev.hpp"

#include <implot.h>
#include <vector>

void phy_dev(App &app, Buffers &data) // Phy layer
{
    static WaterfallData waterfall(1920, 100);
    static std::vector<std::complex<float>> symbols(1920);
    static std::vector<std::complex<float>> raw(3840);
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
        app.begin_scatter<float, std::complex<float>>(label, symbols);
    ImGui::End();
    if (ImGui::Begin("Time domain raw"))
        app.begin_plot_2d<float, std::complex<float>>(label, "I", "Q", raw);
    ImGui::End();

    if (ImGui::Begin("Waterfall"))
        app.run_waterfall("##Waterfall", waterfall, raw);
    ImGui::End();
}

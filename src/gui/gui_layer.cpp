#include "gui/gui_layer.hpp"

#include <vector>
#include "implot.h"

void gui_dev(App &app) // GUI layer
{
    std::vector<float> data(1000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = rand() % 1000;

    if (ImGui::Begin("Plot 1D"))
        app.begin_plot_1d<float, float>("Plot 1D", data);
    ImGui::End();
}


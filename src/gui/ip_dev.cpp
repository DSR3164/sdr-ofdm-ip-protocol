#include "gui/ip_dev.hpp"

#include <vector>
#include "implot.h"

void ip_dev(App &app) // IP layer
{
    std::vector<float> data(1000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = rand() % 1000;

    if (ImGui::Begin("Plot 2D"))
        app.begin_plot_2d("Plot 2D", "I", "Q", data.data(), data.size());
    ImGui::End();
}
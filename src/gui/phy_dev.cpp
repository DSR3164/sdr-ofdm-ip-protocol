#include "gui/phy_dev.hpp"

#include <vector>
#include "implot.h"

void phy_dev(App &app) // Phy layer
{
    std::vector<float> data(1000);
    for (size_t i = 0; i < data.size(); ++i)
        data[i] = rand() % 1000;

    if (ImGui::Begin("Scatter"))
        app.begin_scatter("Scatter", data.data(), data.size());
    ImGui::End();
}
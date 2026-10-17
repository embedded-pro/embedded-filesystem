#include "metrics/boards/BoardDevices.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: metrics.device_models <output.json>\n";
        return 2;
    }

    std::ofstream output{ argv[1] };
    output << metrics::DeviceModelsJson(metrics::norBoardDevices);
    return output ? 0 : 1;
}

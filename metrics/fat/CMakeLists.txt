add_library(metrics.fat STATIC)

target_link_libraries(metrics.fat PUBLIC
    fs.fat
    metrics.device
    metrics.power_cut
)

target_sources(metrics.fat PRIVATE
    FatChecker.cpp
    FatChecker.hpp
    FatSweep.cpp
    FatSweep.hpp
)

add_subdirectory(test)

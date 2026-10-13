add_library(metrics.fat STATIC)

target_link_libraries(metrics.fat PUBLIC
    fs.fat
    fs.fat_test_doubles
    metrics.device
    metrics.power_cut
)

target_sources(metrics.fat PRIVATE
    FatChecker.cpp
    FatChecker.hpp
    FatSweep.cpp
    FatSweep.hpp
)

emfs_add_metrics(metrics.fat_power_cut
    SOURCES FatPowerCut.cpp
    LIBRARIES metrics.fat
    SHARDS 4
)

add_subdirectory(test)

add_library(metrics.fat STATIC)

target_link_libraries(metrics.fat PUBLIC
    fs.fat
    fs.fat_lite
    fs.fat_test_doubles
    metrics.device
    metrics.power_cut
    metrics.report
    metrics.workload
)

target_sources(metrics.fat PRIVATE
    FatChecker.cpp
    FatChecker.hpp
    FatSubject.cpp
    FatSubject.hpp
    FatSweep.cpp
    FatSweep.hpp
    FatTargets.cpp
    FatTargets.hpp
)

emfs_add_metrics(metrics.fat_power_cut
    SOURCES FatPowerCut.cpp
    LIBRARIES metrics.fat
    SHARDS 4
)

emfs_add_metrics(metrics.fat_metrics
    SOURCES FatMetrics.cpp
    LIBRARIES metrics.fat
)

add_subdirectory(test)

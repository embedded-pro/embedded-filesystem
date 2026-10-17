add_library(metrics.nor STATIC)

target_link_libraries(metrics.nor PUBLIC
    fs.nor
    fs.nor_lite
    infra.util
    metrics.device
    metrics.power_cut
    metrics.report
    metrics.workload
)

target_sources(metrics.nor PRIVATE
    NorChecker.cpp
    NorChecker.hpp
    NorConfigurations.hpp
    NorSubject.cpp
    NorSubject.hpp
    NorSweep.cpp
    NorSweep.hpp
    NorTargets.cpp
    NorTargets.hpp
)

emfs_add_metrics(metrics.nor_power_cut
    SOURCES NorPowerCut.cpp
    LIBRARIES metrics.nor
)

emfs_add_metrics(metrics.nor_metrics
    SOURCES NorMetrics.cpp
    LIBRARIES metrics.nor
)

add_subdirectory(test)

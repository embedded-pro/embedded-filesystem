include(FetchContent)

add_library(metrics.baselines STATIC)

target_link_libraries(metrics.baselines PUBLIC
    metrics.workload
)

target_sources(metrics.baselines PRIVATE
    SynchronousDevices.cpp
    SynchronousDevices.hpp
)

add_subdirectory(test)
add_subdirectory(littlefs)
add_subdirectory(fatfs)

add_library(metrics.boards STATIC)

target_link_libraries(metrics.boards PUBLIC
    metrics.fat
    metrics.nor
)

target_sources(metrics.boards PRIVATE
    BoardDevices.cpp
    BoardDevices.hpp
)

add_executable(metrics.device_models)
emil_build_for(metrics.device_models BOOL EMFS_BUILD_METRICS)

target_link_libraries(metrics.device_models PRIVATE
    metrics.boards
)

target_sources(metrics.device_models PRIVATE
    DeviceModelsMain.cpp
)

add_subdirectory(test)

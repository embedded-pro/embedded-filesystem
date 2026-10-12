add_library(metrics.device STATIC)

target_include_directories(metrics.device PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>"
)

target_link_libraries(metrics.device PUBLIC
    hal.interfaces
    infra.event
    infra.timer
    infra.timer_test_helper
)

target_sources(metrics.device PRIVATE
    BlockDeviceStatistics.cpp
    BlockDeviceStatistics.hpp
    DeviceModels.cpp
    DeviceModels.hpp
    FlashStatistics.cpp
    FlashStatistics.hpp
    MeteredBlockDevice.cpp
    MeteredBlockDevice.hpp
    MeteredFlash.cpp
    MeteredFlash.hpp
    VirtualTime.cpp
    VirtualTime.hpp
    VirtualTimeEnvironment.cpp
    VirtualTimeEnvironment.hpp
)

add_subdirectory(test)

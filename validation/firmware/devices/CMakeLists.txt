add_library(validation.devices ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.devices PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.devices PUBLIC
    hal.interfaces
    infra.event
    infra.timer
    services.flash
    validation.timing
)

target_sources(validation.devices PRIVATE
    BlockDeviceRegion.cpp
    BlockDeviceRegion.hpp
    CountedDevice.hpp
    CountingBlockDevice.cpp
    CountingBlockDevice.hpp
    CountingFlash.cpp
    CountingFlash.hpp
    DeviceCounts.cpp
    DeviceCounts.hpp
    NullBlockDevice.cpp
    NullBlockDevice.hpp
    NullFlash.cpp
    NullFlash.hpp
    QuadSpiNor.cpp
    QuadSpiNor.hpp
    RamBlockDevice.cpp
    RamBlockDevice.hpp
    RamCompletion.hpp
    RamFlash.cpp
    RamFlash.hpp
    RequestMeter.cpp
    RequestMeter.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()

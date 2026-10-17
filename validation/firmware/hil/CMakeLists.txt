add_library(validation.hil ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.hil PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.hil PUBLIC
    hal.interfaces
    services.hil
    validation.results
)

target_sources(validation.hil PRIVATE
    DeviceCommands.cpp
    DeviceCommands.hpp
    FirmwareCommands.cpp
    FirmwareCommands.hpp
    NoPins.cpp
    NoPins.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()

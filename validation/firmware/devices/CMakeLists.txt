add_library(validation.devices ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.devices PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.devices PUBLIC
    hal.interfaces
    infra.event
)

target_sources(validation.devices PRIVATE
    NullBlockDevice.cpp
    NullBlockDevice.hpp
    NullFlash.cpp
    NullFlash.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()

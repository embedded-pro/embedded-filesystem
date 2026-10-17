add_library(validation.results ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.results PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.results PUBLIC
    infra.stream
    services.hil
)

target_sources(validation.results PRIVATE
    DeviceRecord.cpp
    DeviceRecord.hpp
    ResultLines.cpp
    ResultLines.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()

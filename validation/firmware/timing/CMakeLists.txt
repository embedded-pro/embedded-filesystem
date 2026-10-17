add_library(validation.timing ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.timing PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.timing PUBLIC
    infra.timer
    infra.util
)

target_sources(validation.timing PRIVATE
    CycleCounter.hpp
    LatencyHistogram.cpp
    LatencyHistogram.hpp
    Nanoseconds.hpp
    Samples.cpp
    Samples.hpp
    Stopwatch.cpp
    Stopwatch.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
    add_subdirectory(test_doubles)
endif()

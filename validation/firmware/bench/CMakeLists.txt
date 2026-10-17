add_library(validation.bench ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.bench PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.bench PUBLIC
    services.hil.commands
    validation.devices
    validation.hil
    validation.results
    validation.timing
)

target_sources(validation.bench PRIVATE
    BenchCommands.cpp
    BenchCommands.hpp
    NorBench.cpp
    NorBench.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
    add_subdirectory(test_doubles)
endif()

add_library(validation.cycles ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.cycles PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.cycles PUBLIC
    infra.event
    infra.stream
    infra.timer
    services.hil.commands
    validation.results
    validation.timing
)

target_sources(validation.cycles PRIVATE
    CycleRun.cpp
    CycleRun.hpp
    CycleSubject.hpp
    CyclesCommands.cpp
    CyclesCommands.hpp
    IdleCycleCounter.cpp
    IdleCycleCounter.hpp
)

add_subdirectory(subjects)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
    add_subdirectory(test_doubles)
endif()

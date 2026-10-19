add_library(validation.layout ${EMFS_EXCLUDE_FROM_ALL} STATIC)

target_include_directories(validation.layout PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../../..>"
)

target_link_libraries(validation.layout PUBLIC
    fs.fat
    services.hil.commands
    validation.devices
    validation.hil
    validation.results
)

target_sources(validation.layout PRIVATE
    FatLayout.cpp
    FatLayout.hpp
    LayoutCommands.cpp
    LayoutCommands.hpp
)

if (EMFS_BUILD_TESTS)
    add_subdirectory(test)
endif()

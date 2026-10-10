add_library(metrics.power_cut STATIC)

target_link_libraries(metrics.power_cut PUBLIC
    metrics.device
    hal.interfaces_test_doubles
)

target_sources(metrics.power_cut PRIVATE
    PowerCutBlockDevice.cpp
    PowerCutBlockDevice.hpp
    PowerCutFlash.cpp
    PowerCutFlash.hpp
    PowerCutSweep.cpp
    PowerCutSweep.hpp
)

add_subdirectory(test)

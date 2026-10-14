add_library(metrics.nor STATIC)

target_link_libraries(metrics.nor PUBLIC
    fs.nor
    infra.util
    metrics.device
    metrics.power_cut
    metrics.report
)

target_sources(metrics.nor PRIVATE
    NorChecker.cpp
    NorChecker.hpp
)

add_subdirectory(test)

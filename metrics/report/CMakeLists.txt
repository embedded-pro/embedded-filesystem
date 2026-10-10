add_library(metrics.report STATIC)

target_link_libraries(metrics.report PUBLIC
    metrics.device
    gtest
)

target_sources(metrics.report PRIVATE
    Report.cpp
    Report.hpp
    ReportEnvironment.cpp
    ReportEnvironment.hpp
)

add_library(metrics.report_main STATIC)

target_link_libraries(metrics.report_main PUBLIC
    metrics.report
    gmock
)

target_sources(metrics.report_main PRIVATE
    MetricsMain.cpp
)

add_subdirectory(test)

add_library(metrics.workload STATIC)

target_link_libraries(metrics.workload PUBLIC
    metrics.device
    fs.interfaces
)

target_sources(metrics.workload PRIVATE
    CompletionQueue.cpp
    CompletionQueue.hpp
    DeviceProbe.cpp
    DeviceProbe.hpp
    FileSystemUnderTest.hpp
    MetricComputations.cpp
    MetricComputations.hpp
    Pattern.cpp
    Pattern.hpp
    Random.cpp
    Random.hpp
    Runner.cpp
    Runner.hpp
    Workloads.cpp
    Workloads.hpp
)

add_subdirectory(test_doubles)
add_subdirectory(test)

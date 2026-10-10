add_library(metrics.workload_test_doubles STATIC)

target_link_libraries(metrics.workload_test_doubles PUBLIC
    metrics.workload
)

target_sources(metrics.workload_test_doubles PRIVATE
    FakeFileSystem.cpp
    FakeFileSystem.hpp
)

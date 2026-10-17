add_library(validation.workload_test_doubles INTERFACE)

target_link_libraries(validation.workload_test_doubles INTERFACE
    gmock
    validation.workload
)

target_sources(validation.workload_test_doubles PRIVATE
    WorkloadMock.hpp
    WorkloadVolumeMock.hpp
)

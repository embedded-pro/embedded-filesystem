add_library(validation.timing_test_doubles INTERFACE)

target_link_libraries(validation.timing_test_doubles INTERFACE
    infra.timer
    validation.timing
)

target_sources(validation.timing_test_doubles PRIVATE
    FakeCycleCounter.hpp
)

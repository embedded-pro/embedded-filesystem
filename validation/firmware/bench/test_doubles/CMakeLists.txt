add_library(validation.bench_test_doubles INTERFACE)

target_link_libraries(validation.bench_test_doubles INTERFACE
    infra.stream
    infra.timer
    validation.devices
)

target_sources(validation.bench_test_doubles PRIVATE
    TimedFlash.hpp
)

add_library(validation.cycles_test_doubles INTERFACE)

target_link_libraries(validation.cycles_test_doubles INTERFACE
    validation.cycles
    validation.timing_test_doubles
)

target_sources(validation.cycles_test_doubles PRIVATE
    CycleFakes.hpp
)

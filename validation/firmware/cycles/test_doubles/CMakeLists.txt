add_library(validation.cycles_test_doubles INTERFACE)

target_link_libraries(validation.cycles_test_doubles INTERFACE
    validation.cycles
)

target_sources(validation.cycles_test_doubles PRIVATE
    CycleFakes.hpp
)

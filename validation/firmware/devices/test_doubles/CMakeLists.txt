add_library(validation.devices_test_doubles INTERFACE)

target_link_libraries(validation.devices_test_doubles INTERFACE
    gmock
    validation.devices
)

target_sources(validation.devices_test_doubles PRIVATE
    CountedDeviceMock.hpp
)

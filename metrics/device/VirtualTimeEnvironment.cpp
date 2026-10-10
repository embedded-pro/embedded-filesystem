#include "metrics/device/VirtualTimeEnvironment.hpp"

namespace metrics
{
    VirtualTime& VirtualTimeEnvironment::Time()
    {
        return time;
    }
}

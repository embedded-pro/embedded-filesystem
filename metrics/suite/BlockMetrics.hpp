#pragma once

#include "metrics/suite/Measurement.hpp"
#include "gtest/gtest.h"

namespace metrics
{
    class BlockMetrics
        : public testing::TestWithParam<BlockSetup>
    {};
}

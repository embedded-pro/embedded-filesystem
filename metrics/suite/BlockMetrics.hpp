#ifndef METRICS_SUITE_BLOCK_METRICS_HPP
#define METRICS_SUITE_BLOCK_METRICS_HPP

#include "metrics/suite/Measurement.hpp"
#include "gtest/gtest.h"

namespace metrics
{
    class BlockMetrics
        : public testing::TestWithParam<BlockSetup>
    {};
}

#endif

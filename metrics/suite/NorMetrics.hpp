#ifndef METRICS_SUITE_NOR_METRICS_HPP
#define METRICS_SUITE_NOR_METRICS_HPP

#include "metrics/suite/Measurement.hpp"
#include "gtest/gtest.h"

namespace metrics
{
    class NorMetrics
        : public testing::TestWithParam<NorSetup>
    {};

    class NorLifetime
        : public testing::TestWithParam<NorSetup>
    {};
}

#endif

#include "validation/firmware/timing/Samples.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    Samples::Samples(infra::BoundedVector<uint32_t>& values)
        : values(values)
    {}

    void Samples::Clear()
    {
        values.clear();
        sum = 0;
    }

    void Samples::Add(uint32_t value)
    {
        values.insert(std::upper_bound(values.begin(), values.end(), value), value);
        sum += value;
    }

    std::size_t Samples::Size() const
    {
        return values.size();
    }

    bool Samples::Full() const
    {
        return values.full();
    }

    uint32_t Samples::Minimum() const
    {
        really_assert(!values.empty());
        return values.front();
    }

    uint32_t Samples::Median() const
    {
        return Percentile(50);
    }

    // The nearest rank, as the host takes its percentiles: the median of an even count is the lower of the two middle values
    uint32_t Samples::Percentile(uint32_t percent) const
    {
        really_assert(!values.empty() && percent >= 1 && percent <= 100);
        auto rank = (static_cast<std::size_t>(percent) * values.size() + 99) / 100;
        return values[rank - 1];
    }

    uint32_t Samples::Maximum() const
    {
        really_assert(!values.empty());
        return values.back();
    }

    uint32_t Samples::Mean() const
    {
        really_assert(!values.empty());
        return static_cast<uint32_t>(sum / values.size());
    }
}

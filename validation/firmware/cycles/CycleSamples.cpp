#include "validation/firmware/cycles/CycleSamples.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <algorithm>

namespace validation
{
    void CycleSamples::Clear()
    {
        samples.clear();
    }

    void CycleSamples::Add(uint32_t cycles)
    {
        samples.push_back(cycles);
    }

    std::size_t CycleSamples::Size() const
    {
        return samples.size();
    }

    uint32_t CycleSamples::Minimum() const
    {
        really_assert(!samples.empty());
        return *std::min_element(samples.begin(), samples.end());
    }

    uint32_t CycleSamples::Median() const
    {
        really_assert(!samples.empty());

        // The nearest rank, as the host takes its percentiles: of an even count, the lower of the two middle values
        auto sorted = samples;
        auto middle = sorted.begin() + static_cast<std::ptrdiff_t>((sorted.size() - 1) / 2);
        std::nth_element(sorted.begin(), middle, sorted.end());
        return *middle;
    }

    uint32_t CycleSamples::Maximum() const
    {
        really_assert(!samples.empty());
        return *std::max_element(samples.begin(), samples.end());
    }
}

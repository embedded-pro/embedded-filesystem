#ifndef VALIDATION_FIRMWARE_CYCLES_CYCLE_SAMPLES_HPP
#define VALIDATION_FIRMWARE_CYCLES_CYCLE_SAMPLES_HPP

#include "infra/util/BoundedVector.hpp"
#include <cstddef>
#include <cstdint>

namespace validation
{
    class CycleSamples
    {
    public:
        static constexpr std::size_t capacity = 200;

        void Clear();
        void Add(uint32_t cycles);

        std::size_t Size() const;
        uint32_t Minimum() const;
        uint32_t Median() const;
        uint32_t Maximum() const;

    private:
        infra::BoundedVector<uint32_t>::WithMaxSize<capacity> samples;
    };
}

#endif

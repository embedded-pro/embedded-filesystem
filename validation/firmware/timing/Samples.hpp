#ifndef VALIDATION_FIRMWARE_TIMING_SAMPLES_HPP
#define VALIDATION_FIRMWARE_TIMING_SAMPLES_HPP

#include "infra/util/BoundedVector.hpp"
#include "infra/util/WithStorage.hpp"
#include <cstddef>
#include <cstdint>

namespace validation
{
    class Samples
    {
    public:
        template<std::size_t Capacity>
        using WithCapacity = infra::WithStorage<Samples, infra::BoundedVector<uint32_t>::WithMaxSize<Capacity>>;

        explicit Samples(infra::BoundedVector<uint32_t>& values);
        Samples(const Samples& other) = delete;
        Samples& operator=(const Samples& other) = delete;
        ~Samples() = default;

        void Clear();
        void Add(uint32_t value);

        std::size_t Size() const;
        bool Full() const;
        uint32_t Minimum() const;
        uint32_t Median() const;
        uint32_t Percentile(uint32_t percent) const;
        uint32_t Maximum() const;
        uint32_t Mean() const;

    private:
        infra::BoundedVector<uint32_t>& values;
        uint64_t sum{ 0 };
    };
}

#endif

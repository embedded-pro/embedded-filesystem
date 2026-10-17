#ifndef VALIDATION_FIRMWARE_WORKLOAD_RANDOM_OVERWRITE_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_RANDOM_OVERWRITE_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "metrics/workload/Random.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // L3: 64 overwrites of 512 B at random offsets, each synced, in a file of 64 KiB, 1 MiB and 4 MiB, scaled
    class RandomOverwrite
        : public Workload
    {
    public:
        static constexpr uint32_t overwriteSize = 512;
        static constexpr uint32_t overwrites = 64;

        explicit RandomOverwrite(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        uint32_t Size() const;
        void Create();
        void Open();
        void Overwrite();
        void Cleanup();

    private:
        static constexpr std::array<uint32_t, 3> sizes{ 64 * kibibyte, mebibyte, 4 * mebibyte };

        WorkloadTools& tools;
        WorkloadParameters parameters;
        metrics::Random random{ 0 };
        std::size_t file{ 0 };
        uint32_t fileId{ 0 };
        uint32_t overwrite{ 0 };
        infra::AutoResetFunction<void()> onDone;
        std::array<uint8_t, overwriteSize> data{};
    };
}

#endif

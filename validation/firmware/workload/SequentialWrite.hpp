#ifndef VALIDATION_FIRMWARE_WORKLOAD_SEQUENTIAL_WRITE_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_SEQUENTIAL_WRITE_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // L1: one file of 1 MiB and one of 4 MiB, scaled, in chunks of 64 B, 512 B and 4 KiB, each removed after it is written
    class SequentialWrite
        : public Workload
    {
    public:
        explicit SequentialWrite(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        void WriteFile();
        void Cleanup();

    private:
        static constexpr std::array<uint32_t, 2> sizes{ mebibyte, 4 * mebibyte };
        static constexpr std::array<uint32_t, 3> chunks{ 64, 512, chunkSize };

        WorkloadTools& tools;
        WorkloadParameters parameters;
        std::size_t file{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif

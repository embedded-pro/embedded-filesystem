#ifndef VALIDATION_FIRMWARE_WORKLOAD_READ_PATTERNS_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_READ_PATTERNS_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "metrics/workload/Random.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // L6: a sequential read of 1 MiB, scaled; 64 reads of 512 B at random offsets and one at the end, in files of 64 KiB,
    // 1 MiB and 4 MiB, scaled; and lists and opens by path in directories of 10, 100 and 1000 entries
    class ReadPatterns
        : public Workload
    {
    public:
        static constexpr uint32_t randomReadSize = 512;
        static constexpr uint32_t randomReads = 64;
        static constexpr uint32_t lookups = 32;

        explicit ReadPatterns(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        void CreateSequential();
        void ReadChunk();
        void StartRandom();
        uint32_t RandomFileSize() const;
        void CreateRandom();
        void OpenRandom();
        void ReadAtRandom();
        void ReadAtEnd();
        void NextRandomFile();
        void CreateDirectory();
        void CreateEntry();
        void List();
        void LookUp();

    private:
        static constexpr std::array<uint32_t, 3> randomFileSizes{ 64 * kibibyte, mebibyte, 4 * mebibyte };
        static constexpr std::array<uint32_t, 3> directorySizes{ 10, 100, 1000 };

        WorkloadTools& tools;
        WorkloadParameters parameters;
        metrics::Random random{ 0 };
        std::size_t step{ 0 };
        uint32_t count{ 0 };
        WorkloadPath directory;
        WorkloadPath path;
        infra::AutoResetFunction<void()> onDone;
        std::array<uint8_t, chunkSize> data{};
    };
}

#endif

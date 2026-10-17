#ifndef VALIDATION_FIRMWARE_WORKLOAD_SMALL_FILES_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_SMALL_FILES_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "metrics/workload/Random.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <cstdint>

namespace validation
{
    // L4: 200 files of 100 B to 8 KiB, of which the latest 16 are kept; the files are not scaled, since 200 divided by
    // the scale divisor of the boards would leave fewer files than are kept
    class SmallFiles
        : public Workload
    {
    public:
        static constexpr uint32_t files = 200;
        static constexpr uint32_t window = 16;
        static constexpr uint32_t smallestFile = 100;
        static constexpr uint32_t largestFile = 8 * kibibyte;

        explicit SmallFiles(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        void Start();
        void WriteFile();
        void Written();

    private:
        WorkloadTools& tools;
        metrics::Random random{ 0 };
        uint32_t index{ 0 };
        WorkloadPath path;
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif

#ifndef VALIDATION_FIRMWARE_WORKLOAD_MOUNT_SCALING_HPP
#define VALIDATION_FIRMWARE_WORKLOAD_MOUNT_SCALING_HPP

#include "infra/util/AutoResetFunction.hpp"
#include "validation/firmware/workload/VolumeFiller.hpp"
#include "validation/firmware/workload/Workload.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace validation
{
    // L7: a mount of the volume when it is 10 %, 50 % and 90 % full
    class MountScaling
        : public Workload
    {
    public:
        explicit MountScaling(WorkloadTools& tools);

        infra::BoundedConstString Name() const override;
        bool Fills() const override;
        void Run(const WorkloadParameters& parameters, const infra::Function<void()>& onDone) override;

    private:
        void Prepare();
        void Mount();

    private:
        static constexpr std::array<uint32_t, 3> fillLevels{ 10, 50, 90 };

        WorkloadTools& tools;
        WorkloadParameters parameters;
        std::size_t level{ 0 };
        infra::AutoResetFunction<void()> onDone;
    };
}

#endif

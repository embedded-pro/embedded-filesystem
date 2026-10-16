#include "validation/firmware/boards/Memories.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <bit>
#include <cstddef>
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        constexpr std::size_t regionsOfTheUnit = 16;

        // As in the examples of ST: without a region, the Cortex-M7 may read the external memories speculatively, also where no memory is
        void DenyExternalMemories()
        {
            MPU_Region_InitTypeDef init{};
            init.Enable = MPU_REGION_ENABLE;
            init.Number = MPU_REGION_NUMBER0;
            init.BaseAddress = 0;
            init.Size = MPU_REGION_SIZE_4GB;
            init.SubRegionDisable = 0x87;
            init.AccessPermission = MPU_REGION_NO_ACCESS;
            init.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
            init.IsShareable = MPU_ACCESS_SHAREABLE;
            init.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
            init.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;
            init.TypeExtField = MPU_TEX_LEVEL0;
            HAL_MPU_ConfigRegion(&init);
        }

        void ConfigureRegion(const MemoryRegion& region, uint8_t number)
        {
            really_assert(std::has_single_bit(region.size) && region.size >= 32 && region.address % region.size == 0);

            MPU_Region_InitTypeDef init{};
            init.Enable = MPU_REGION_ENABLE;
            init.Number = number;
            init.BaseAddress = region.address;
            init.Size = static_cast<uint8_t>(std::countr_zero(region.size) - 1);
            init.AccessPermission = MPU_REGION_FULL_ACCESS;
            init.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
            init.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
            init.IsBufferable = MPU_ACCESS_BUFFERABLE;

            if (region.type == MemoryType::device)
            {
                init.TypeExtField = MPU_TEX_LEVEL0;
                init.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
            }
            else
            {
                init.TypeExtField = MPU_TEX_LEVEL1;
                init.IsCacheable = MPU_ACCESS_CACHEABLE;
            }

            HAL_MPU_ConfigRegion(&init);
        }
    }

    void ConfigureMemories(infra::MemoryRange<const MemoryRegion> regions)
    {
        really_assert(regions.size() < regionsOfTheUnit);

        HAL_MPU_Disable();
        DenyExternalMemories();

        uint8_t number = MPU_REGION_NUMBER1;
        for (const auto& region : regions)
            ConfigureRegion(region, number++);

        HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
        SCB_EnableICache();
        SCB_EnableDCache();
    }
}

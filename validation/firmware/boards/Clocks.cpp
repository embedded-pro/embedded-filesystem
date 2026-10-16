#include "validation/firmware/boards/Clocks.hpp"
#include "infra/util/ReallyAssert.hpp"
#include DEVICE_HEADER

namespace validation
{
    namespace
    {
        void ConfigureSupply()
        {
#if defined(USE_PWR_DIRECT_SMPS_SUPPLY)
            really_assert(HAL_PWREx_ConfigSupply(PWR_DIRECT_SMPS_SUPPLY) == HAL_OK);
#endif

            __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

            while (!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY))
            {
            }
        }

        void StartPll()
        {
            RCC_OscInitTypeDef oscillators{};
            oscillators.OscillatorType = RCC_OSCILLATORTYPE_HSE;
            oscillators.HSEState = RCC_HSE_ON;
            oscillators.HSIState = RCC_HSI_OFF;
            oscillators.CSIState = RCC_CSI_OFF;
            oscillators.PLL.PLLState = RCC_PLL_ON;
            oscillators.PLL.PLLSource = RCC_PLLSOURCE_HSE;
            oscillators.PLL.PLLM = 5;
            oscillators.PLL.PLLN = 160;
            oscillators.PLL.PLLP = 2;
            oscillators.PLL.PLLQ = 4;
            oscillators.PLL.PLLR = 2;
            oscillators.PLL.PLLRGE = RCC_PLL1VCIRANGE_2;
            oscillators.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
            oscillators.PLL.PLLFRACN = 0;
            really_assert(HAL_RCC_OscConfig(&oscillators) == HAL_OK);
        }

        void SelectPll()
        {
            RCC_ClkInitTypeDef clocks{};
            clocks.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_D1PCLK1 | RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2 | RCC_CLOCKTYPE_D3PCLK1;
            clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
            clocks.SYSCLKDivider = RCC_SYSCLK_DIV1;
            clocks.AHBCLKDivider = RCC_HCLK_DIV2;
            clocks.APB3CLKDivider = RCC_APB3_DIV2;
            clocks.APB1CLKDivider = RCC_APB1_DIV2;
            clocks.APB2CLKDivider = RCC_APB2_DIV2;
            clocks.APB4CLKDivider = RCC_APB4_DIV2;
            really_assert(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_4) == HAL_OK);
        }
    }

    void ConfigureSystemClock()
    {
        ConfigureSupply();
        StartPll();
        SelectPll();
    }
}

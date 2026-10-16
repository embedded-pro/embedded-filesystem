#include "services/hil/HilPinPool.hpp"
#include "validation/firmware/hil/NoPins.hpp"
#include "gmock/gmock.h"

TEST(NoPinsTest, offers_no_pin)
{
    validation::NoPins factory;
    const services::HilPinId pin{ 0, 0 };

    EXPECT_FALSE(factory.IsValid(pin));
    EXPECT_FALSE(factory.SupportsFunction(pin, 0, 1));
    EXPECT_FALSE(factory.SupportsAnalog(pin));
    EXPECT_FALSE(factory.SupportsInterrupt(pin));
    EXPECT_EQ(std::nullopt, factory.ParseDrive("high"));
}

TEST(NoPinsTest, pool_claims_no_pin)
{
    validation::NoPins factory;
    services::HilPinPool::WithCapacity<1> pool{ factory };
    hal::GpioPin* pin = nullptr;

    EXPECT_EQ(services::HilStatus::pin, pool.Claim(services::HilPinId{ 1, 2 }, services::HilOwners::gpio, services::HilPinPool::Use::exclusive, pin));
    EXPECT_EQ(nullptr, pin);
}

TEST(NoPinsTest, constructs_an_inert_pin)
{
    validation::NoPins factory;

    auto& pin = factory.Construct(0, services::HilPinId{ 0, 0 }, {});
    pin.Set(true);

    EXPECT_FALSE(pin.Get());
    factory.Destroy(0);
}

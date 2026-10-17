#include "hal/interfaces/test_doubles/QuadSpiStub.hpp"
#include "infra/event/test_helper/EventDispatcherFixture.hpp"
#include "infra/util/BoundedVector.hpp"
#include "infra/util/test_helper/MockCallback.hpp"
#include "services/flash/FlashGeometryQuad.hpp"
#include "validation/firmware/devices/QuadSpiNor.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>
#include <initializer_list>

namespace
{
    class FakeGeometry
        : public services::FlashGeometryQuad
    {
    public:
        uint32_t NrOfSubSectors() const override
        {
            return subSectors;
        }

        uint32_t SizeSector() const override
        {
            return 65536;
        }

        uint32_t SizeSubSector() const override
        {
            return 4096;
        }

        uint32_t SizePage() const override
        {
            return 256;
        }

        bool ExtendedAddressing() const override
        {
            return true;
        }

        uint8_t EraseSubSectorCommand() const override
        {
            return 0x21;
        }

        uint8_t EraseSectorCommand() const override
        {
            return 0xdc;
        }

        uint8_t EraseBulkCommand() const override
        {
            return 0xc7;
        }

        uint8_t PageProgramCommand() const override
        {
            return 0x34;
        }

        uint8_t ReadDataCommand() const override
        {
            return 0xec;
        }

        uint8_t ReadDummyCycles() const override
        {
            return dummyCycles;
        }

        uint32_t subSectors{ 16384 };
        uint8_t dummyCycles{ 10 };
    };

    hal::QuadSpi::Header Command(uint8_t command)
    {
        return hal::QuadSpi::Header{ command, {}, {}, 0 };
    }

    class QuadSpiNorTest
        : public testing::Test
        , public infra::EventDispatcherFixture
    {
    public:
        void ExpectIdentification(std::initializer_list<uint8_t> identification)
        {
            EXPECT_CALL(spi, ReceiveDataMock(Command(0x9f), hal::QuadSpi::Lines::SingleSpeed())).WillOnce(testing::Return(Keep(identification)));
        }

        void ExpectVolatileConfiguration(uint8_t value)
        {
            EXPECT_CALL(spi, ReceiveDataMock(Command(0x85), hal::QuadSpi::Lines::SingleSpeed())).WillOnce(testing::Return(Keep({ value })));
        }

        void ExpectWriteOfVolatileConfiguration(uint8_t value)
        {
            EXPECT_CALL(spi, SendDataMock(Command(0x06), testing::Truly([](infra::ConstByteRange data)
                                                             {
                                                                 return data.empty();
                                                             }),
                                 hal::QuadSpi::Lines::SingleSpeed()));
            EXPECT_CALL(spi, SendDataMock(Command(0x81), testing::Truly([value](infra::ConstByteRange data)
                                                             {
                                                                 return data.size() == 1 && data[0] == value;
                                                             }),
                                 hal::QuadSpi::Lines::SingleSpeed()));
        }

        void Start(bool present)
        {
            nor.Start(geometry, [this](bool result)
                {
                    done.callback(result);
                });
            EXPECT_CALL(done, callback(present));
            ExecuteAllActions();
        }

        infra::ConstByteRange Keep(std::initializer_list<uint8_t> bytes)
        {
            auto& storage = kept[keptCount++];
            storage.assign(bytes.begin(), bytes.end());
            return infra::MakeRange(storage);
        }

        testing::StrictMock<hal::QuadSpiStub> spi;
        FakeGeometry geometry;
        validation::QuadSpiNor nor{ spi, 4096 };
        testing::StrictMock<infra::MockCallback<void(bool)>> done;
        std::array<infra::BoundedVector<uint8_t>::WithMaxSize<4>, 4> kept;
        std::size_t keptCount{ 0 };
    };
}

TEST_F(QuadSpiNorTest, a_micron_flash_with_the_default_dummy_cycles_becomes_a_region_of_its_first_sectors)
{
    ExpectIdentification({ 0x20, 0xba, 0x20 });
    ExpectVolatileConfiguration(0xfb);
    Start(true);

    EXPECT_TRUE(nor.Present());
    EXPECT_EQ(0x20ba20, nor.JedecId());
    EXPECT_EQ(4096, nor.NumberOfSectors());
    EXPECT_EQ(4096, nor.SizeOfSector(4095));
    EXPECT_EQ(2, nor.SectorOfAddress(8192));
    EXPECT_EQ(12288, nor.AddressOfSector(3));
}

TEST_F(QuadSpiNorTest, the_dummy_cycles_of_the_volatile_configuration_are_aligned_with_those_of_the_geometry)
{
    testing::InSequence sequence;
    ExpectIdentification({ 0x20, 0xba, 0x20 });
    ExpectVolatileConfiguration(0x8b);
    ExpectWriteOfVolatileConfiguration(0xab);
    ExpectVolatileConfiguration(0xab);
    Start(true);
}

TEST_F(QuadSpiNorTest, a_volatile_configuration_that_does_not_take_the_dummy_cycles_leaves_the_flash_absent)
{
    testing::InSequence sequence;
    ExpectIdentification({ 0x20, 0xba, 0x20 });
    ExpectVolatileConfiguration(0x8b);
    ExpectWriteOfVolatileConfiguration(0xab);
    ExpectVolatileConfiguration(0x8b);
    Start(false);

    EXPECT_FALSE(nor.Present());
}

TEST_F(QuadSpiNorTest, a_flash_of_another_maker_keeps_its_configuration)
{
    ExpectIdentification({ 0xef, 0x40, 0x19 });
    Start(true);
}

TEST_F(QuadSpiNorTest, a_micron_flash_read_without_dummy_cycles_keeps_its_configuration)
{
    geometry.dummyCycles = 0;
    ExpectIdentification({ 0x20, 0xba, 0x20 });
    Start(true);
}

TEST_F(QuadSpiNorTest, a_flash_that_does_not_answer_is_absent)
{
    ExpectIdentification({ 0xff, 0xff, 0xff });
    Start(false);

    ExpectIdentification({ 0x00, 0x00, 0x00 });
    Start(false);
}

TEST_F(QuadSpiNorTest, a_flash_smaller_than_the_region_is_absent)
{
    geometry.subSectors = 4095;
    ExpectIdentification({ 0xef, 0x40, 0x19 });
    Start(false);
}

TEST_F(QuadSpiNorTest, a_present_flash_forwards_its_requests)
{
    ExpectIdentification({ 0xef, 0x40, 0x19 });
    Start(true);

    std::array<uint8_t, 2> buffer{};
    EXPECT_CALL(spi, ReceiveDataMock(testing::_, testing::_)).WillOnce(testing::Return(Keep({ 0x12, 0x34 })));
    nor.ReadBuffer(infra::MakeRange(buffer), 0x100, [this]
        {
            done.callback(true);
        });
    EXPECT_CALL(done, callback(true));
    ExecuteAllActions();

    EXPECT_EQ((std::array<uint8_t, 2>{ { 0x12, 0x34 } }), buffer);
}

TEST_F(QuadSpiNorTest, a_present_flash_forwards_its_programs_and_erases)
{
    ExpectIdentification({ 0xef, 0x40, 0x19 });
    Start(true);

    EXPECT_CALL(spi, SendDataMock(testing::_, testing::_, testing::_)).Times(testing::AnyNumber());
    EXPECT_CALL(spi, PollStatusMock(testing::_, testing::_, testing::_, testing::_, testing::_)).WillRepeatedly([this]
        {
            infra::EventDispatcher::Instance().Schedule([this]
                {
                    spi.onDone();
                });
        });

    std::array<uint8_t, 2> data{ { 0x12, 0x34 } };
    nor.WriteBuffer(infra::MakeConstRange(data), 0x100, [this]
        {
            done.callback(true);
        });
    EXPECT_CALL(done, callback(true));
    ExecuteAllActions();

    nor.EraseSectors(1, 2, [this]
        {
            done.callback(false);
        });
    EXPECT_CALL(done, callback(false));
    ExecuteAllActions();
}

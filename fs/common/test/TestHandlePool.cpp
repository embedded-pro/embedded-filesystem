#include "fs/common/HandlePool.hpp"
#include "gmock/gmock.h"

namespace
{
    class Interface
    {
    protected:
        Interface() = default;
        Interface(const Interface& other) = delete;
        Interface& operator=(const Interface& other) = delete;
        ~Interface() = default;

    public:
        virtual int Identity() const = 0;
    };

    class TestHandle
        : public fs::PooledHandle
        , public Interface
    {
    public:
        explicit TestHandle(int& constructions)
            : identity(++constructions)
        {}

        int Identity() const override
        {
            return identity;
        }

        MOCK_METHOD(void, UnReferenced, (), (override));
        using PooledHandle::Release;
        using PooledHandle::Share;

    private:
        int identity;
    };

    class HandlePoolTest
        : public testing::Test
    {
    public:
        int constructions{ 0 };
        fs::HandlePool<testing::StrictMock<TestHandle>, 2> pool{ constructions };
    };
}

TEST_F(HandlePoolTest, allocates_a_handle)
{
    auto handle = pool.Allocate();

    EXPECT_NE(nullptr, handle);
    EXPECT_TRUE(handle->InUse());
    EXPECT_FALSE(pool.Exhausted());

    EXPECT_CALL(*handle, UnReferenced());
}

TEST_F(HandlePoolTest, allocation_fails_when_every_handle_is_in_use)
{
    auto first = pool.Allocate();
    auto second = pool.Allocate();

    EXPECT_NE(first, second);
    EXPECT_TRUE(pool.Exhausted());
    EXPECT_EQ(nullptr, pool.Allocate());

    EXPECT_CALL(*first, UnReferenced());
    EXPECT_CALL(*second, UnReferenced());
}

TEST_F(HandlePoolTest, releasing_the_last_reference_notifies_the_handle)
{
    auto handle = pool.Allocate();
    auto& object = *handle;

    EXPECT_CALL(object, UnReferenced());
    handle = nullptr;
}

TEST_F(HandlePoolTest, releasing_a_copy_does_not_notify_the_handle)
{
    auto handle = pool.Allocate();
    auto copy = handle;

    copy = nullptr;

    EXPECT_CALL(*handle, UnReferenced());
}

TEST_F(HandlePoolTest, weak_reference_delays_the_notification)
{
    auto handle = pool.Allocate();
    auto& object = *handle;
    infra::WeakPtr<testing::StrictMock<TestHandle>> weak{ handle };

    handle = nullptr;

    EXPECT_CALL(object, UnReferenced());
    weak = nullptr;
}

TEST_F(HandlePoolTest, unreferenced_handle_stays_in_use_until_released)
{
    auto handle = pool.Allocate();
    auto& object = *handle;
    EXPECT_CALL(object, UnReferenced());
    handle = nullptr;

    EXPECT_TRUE(object.InUse());
    object.Release();
    EXPECT_FALSE(object.InUse());
}

TEST_F(HandlePoolTest, released_handle_is_allocated_again)
{
    auto handle = pool.Allocate();
    auto other = pool.Allocate();
    auto& object = *handle;
    EXPECT_CALL(object, UnReferenced()).WillOnce([&object]
        {
            object.Release();
        });
    handle = nullptr;

    auto again = pool.Allocate();

    EXPECT_EQ(&object, again.operator->());

    EXPECT_CALL(*again, UnReferenced());
    EXPECT_CALL(*other, UnReferenced());
}

TEST_F(HandlePoolTest, handles_are_constructed_once_with_the_pool_arguments)
{
    EXPECT_EQ(2, constructions);

    auto first = pool.Allocate();
    auto second = pool.Allocate();

    EXPECT_EQ(2, constructions);
    EXPECT_EQ(1, first->Identity());
    EXPECT_EQ(2, second->Identity());

    EXPECT_CALL(*first, UnReferenced());
    EXPECT_CALL(*second, UnReferenced());
}

TEST_F(HandlePoolTest, handle_is_shared_as_its_interface)
{
    auto handle = pool.Allocate();
    auto& object = *handle;
    infra::SharedPtr<Interface> shared = handle;
    handle = nullptr;

    EXPECT_EQ(1, shared->Identity());

    EXPECT_CALL(object, UnReferenced());
    shared = nullptr;
}

TEST_F(HandlePoolTest, handle_shared_by_itself_stays_referenced)
{
    auto handle = pool.Allocate();
    auto& object = *handle;
    auto self = object.Share(object);
    handle = nullptr;

    EXPECT_EQ(&object, self.operator->());

    EXPECT_CALL(object, UnReferenced());
    self = nullptr;
}

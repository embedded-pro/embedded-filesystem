#include "fs/common/LiteLifecycle.hpp"
#include "gtest/gtest.h"

namespace
{
    class LiteLifecycleTest
        : public testing::Test
    {
    public:
        void Mount()
        {
            ASSERT_TRUE(lifecycle.StartMount());
            lifecycle.Mounted(fs::Result::success);
        }

        fs::LiteLifecycle lifecycle;
    };
}

TEST_F(LiteLifecycleTest, starts_unmounted)
{
    EXPECT_FALSE(lifecycle.IsMounted());
    EXPECT_FALSE(lifecycle.Changing());
    EXPECT_EQ(fs::Result::notMounted, lifecycle.Unavailable());
}

TEST_F(LiteLifecycleTest, a_mount_that_succeeds_mounts)
{
    EXPECT_TRUE(lifecycle.StartMount());
    EXPECT_TRUE(lifecycle.Changing());
    EXPECT_FALSE(lifecycle.IsMounted());

    lifecycle.Mounted(fs::Result::success);
    EXPECT_TRUE(lifecycle.IsMounted());
    EXPECT_FALSE(lifecycle.Changing());
}

TEST_F(LiteLifecycleTest, only_a_mount_in_progress_is_mounting)
{
    EXPECT_FALSE(lifecycle.Mounting());

    ASSERT_TRUE(lifecycle.StartMount());
    EXPECT_TRUE(lifecycle.Mounting());

    lifecycle.Mounted(fs::Result::success);
    EXPECT_FALSE(lifecycle.Mounting());

    ASSERT_TRUE(lifecycle.StartUnmount());
    EXPECT_FALSE(lifecycle.Mounting());
}

TEST_F(LiteLifecycleTest, a_mount_is_refused_while_mounting_mounted_or_unmounting)
{
    ASSERT_TRUE(lifecycle.StartMount());
    EXPECT_FALSE(lifecycle.StartMount());

    lifecycle.Mounted(fs::Result::success);
    EXPECT_FALSE(lifecycle.StartMount());

    ASSERT_TRUE(lifecycle.StartUnmount());
    EXPECT_FALSE(lifecycle.StartMount());
}

TEST_F(LiteLifecycleTest, a_mount_that_fails_leaves_the_volume_unavailable_and_may_start_again)
{
    ASSERT_TRUE(lifecycle.StartMount());
    lifecycle.Mounted(fs::Result::corrupted);

    EXPECT_FALSE(lifecycle.IsMounted());
    EXPECT_FALSE(lifecycle.Changing());
    EXPECT_EQ(fs::Result::notMounted, lifecycle.Unavailable());
    EXPECT_TRUE(lifecycle.StartMount());
}

TEST_F(LiteLifecycleTest, an_unmount_starts_only_from_mounted)
{
    EXPECT_FALSE(lifecycle.StartUnmount());

    ASSERT_TRUE(lifecycle.StartMount());
    EXPECT_FALSE(lifecycle.StartUnmount());

    lifecycle.Mounted(fs::Result::success);
    EXPECT_TRUE(lifecycle.StartUnmount());
    EXPECT_TRUE(lifecycle.Changing());
    EXPECT_FALSE(lifecycle.IsMounted());
    EXPECT_FALSE(lifecycle.StartUnmount());

    lifecycle.Unmounted();
    EXPECT_FALSE(lifecycle.Changing());
    EXPECT_EQ(fs::Result::notMounted, lifecycle.Unavailable());
    EXPECT_TRUE(lifecycle.StartMount());
}

TEST_F(LiteLifecycleTest, a_removed_medium_leaves_a_mounted_volume_unavailable_until_it_mounts_again)
{
    Mount();

    EXPECT_TRUE(lifecycle.RemoveMedia());
    EXPECT_FALSE(lifecycle.IsMounted());
    EXPECT_FALSE(lifecycle.Changing());
    EXPECT_EQ(fs::Result::mediaRemoved, lifecycle.Unavailable());
    EXPECT_FALSE(lifecycle.StartUnmount());

    EXPECT_TRUE(lifecycle.StartMount());
    lifecycle.Mounted(fs::Result::deviceError);
    EXPECT_EQ(fs::Result::notMounted, lifecycle.Unavailable());
}

TEST_F(LiteLifecycleTest, a_removed_medium_is_ignored_unless_mounted)
{
    EXPECT_FALSE(lifecycle.RemoveMedia());

    ASSERT_TRUE(lifecycle.StartMount());
    EXPECT_FALSE(lifecycle.RemoveMedia());

    lifecycle.Mounted(fs::Result::success);
    ASSERT_TRUE(lifecycle.StartUnmount());
    EXPECT_FALSE(lifecycle.RemoveMedia());

    lifecycle.Unmounted();
    EXPECT_EQ(fs::Result::notMounted, lifecycle.Unavailable());
}

TEST_F(LiteLifecycleTest, a_mount_that_reports_a_removed_medium_leaves_it_unavailable)
{
    ASSERT_TRUE(lifecycle.StartMount());
    lifecycle.Mounted(fs::Result::mediaRemoved);

    EXPECT_EQ(fs::Result::mediaRemoved, lifecycle.Unavailable());
}

#include "fs/common/Path.hpp"
#include "gtest/gtest.h"

namespace
{
    constexpr fs::PathLimits limits{ 4, 12 };

    fs::Result Validate(infra::BoundedConstString path)
    {
        return fs::ValidatePath(path, limits);
    }
}

TEST(PathTest, root_is_valid)
{
    EXPECT_EQ(fs::Result::success, Validate("/"));
}

TEST(PathTest, nested_path_is_valid)
{
    EXPECT_EQ(fs::Result::success, Validate("/a/bc/d"));
}

TEST(PathTest, empty_path_is_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate(""));
}

TEST(PathTest, relative_path_is_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate("a/b"));
}

TEST(PathTest, relative_path_longer_than_maximum_is_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate("abcd/abcd/abcd"));
}

TEST(PathTest, empty_component_is_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate("/a//b"));
    EXPECT_EQ(fs::Result::invalidPath, Validate("//"));
}

TEST(PathTest, trailing_separator_is_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate("/a/"));
}

TEST(PathTest, dot_and_dot_dot_components_are_invalid)
{
    EXPECT_EQ(fs::Result::invalidPath, Validate("/."));
    EXPECT_EQ(fs::Result::invalidPath, Validate("/a/.."));
    EXPECT_EQ(fs::Result::invalidPath, Validate("/a/./b"));
}

TEST(PathTest, names_containing_dots_are_valid)
{
    EXPECT_EQ(fs::Result::success, Validate("/.a"));
    EXPECT_EQ(fs::Result::success, Validate("/a..b"));
    EXPECT_EQ(fs::Result::success, Validate("/..."));
}

TEST(PathTest, nul_character_is_invalid)
{
    const char path[] = { '/', 'a', '\0', 'b' };

    EXPECT_EQ(fs::Result::invalidPath, Validate(infra::BoundedConstString(path, sizeof(path))));
}

TEST(PathTest, name_of_maximum_length_is_valid)
{
    EXPECT_EQ(fs::Result::success, Validate("/abcd/e"));
}

TEST(PathTest, name_longer_than_maximum_is_too_long)
{
    EXPECT_EQ(fs::Result::nameTooLong, Validate("/abcde"));
}

TEST(PathTest, path_of_maximum_length_is_valid)
{
    EXPECT_EQ(fs::Result::success, Validate("/abc/abc/abc"));
}

TEST(PathTest, path_longer_than_maximum_is_too_long)
{
    EXPECT_EQ(fs::Result::nameTooLong, Validate("/abc/abc/abcd"));
}

TEST(PathTest, root_has_no_components)
{
    fs::PathComponents components{ "/" };

    EXPECT_TRUE(components.Done());
    EXPECT_EQ(std::nullopt, components.Next());
}

TEST(PathTest, components_are_returned_in_order)
{
    fs::PathComponents components{ "/a/bc/d" };

    EXPECT_EQ("a", components.Next());
    EXPECT_EQ("bc", components.Next());
    EXPECT_EQ("d", components.Next());
    EXPECT_EQ(std::nullopt, components.Next());
}

TEST(PathTest, done_after_last_component)
{
    fs::PathComponents components{ "/a/b" };

    components.Next();
    EXPECT_FALSE(components.Done());
    components.Next();
    EXPECT_TRUE(components.Done());
}

TEST(PathTest, split_last_of_nested_path)
{
    auto split = fs::SplitLast("/a/b/c");

    EXPECT_EQ("/a/b", split.parent);
    EXPECT_EQ("c", split.name);
}

TEST(PathTest, split_last_of_top_level_path)
{
    auto split = fs::SplitLast("/a");

    EXPECT_EQ("/", split.parent);
    EXPECT_EQ("a", split.name);
}

TEST(PathTest, split_last_of_root)
{
    auto split = fs::SplitLast("/");

    EXPECT_EQ("/", split.parent);
    EXPECT_TRUE(split.name.empty());
}

TEST(PathTest, only_slash_is_root)
{
    EXPECT_TRUE(fs::IsRoot("/"));
    EXPECT_FALSE(fs::IsRoot("/a"));
    EXPECT_FALSE(fs::IsRoot(""));
}

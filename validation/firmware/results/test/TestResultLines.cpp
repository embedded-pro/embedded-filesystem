#include "infra/util/BoundedString.hpp"
#include "services/hil/test/HilFixture.hpp"
#include "validation/firmware/results/ResultLines.hpp"
#include "gmock/gmock.h"
#include <array>
#include <cstdint>

class ResultLinesTest
    : public services::HilFixture
{
public:
    template<std::size_t Size>
    static infra::BoundedString::WithStorage<Size> Filler()
    {
        return infra::BoundedString::WithStorage<Size>(Size, 'x');
    }

    validation::ResultLines results{ response };
};

TEST_F(ResultLinesTest, a_record_is_an_event_line_with_its_id_and_pairs)
{
    results.Record("bench").Add("device", "qspi").Add("n", 3);

    EXPECT_EQ("\r\nEVT bench id=1 device=qspi n=3\r\n", Output());
}

TEST_F(ResultLinesTest, records_are_numbered_in_order)
{
    results.Record("bench");
    results.Record("cycles");

    EXPECT_EQ("\r\nEVT bench id=1\r\n\r\nEVT cycles id=2\r\n", Output());
}

TEST_F(ResultLinesTest, numbers_take_64_bits)
{
    results.Record("phase").Add("elapsedNs", 18446744073709551615u);

    EXPECT_EQ("\r\nEVT phase id=1 elapsedNs=18446744073709551615\r\n", Output());
}

TEST_F(ResultLinesTest, lists_are_separated_by_commas)
{
    const std::array<uint32_t, 3> buckets{ { 0, 12, 7 } };

    results.Record("phase").Add("histogram", infra::MakeRange(buckets));

    EXPECT_EQ("\r\nEVT phase id=1 histogram=0,12,7\r\n", Output());
}

TEST_F(ResultLinesTest, an_empty_list_has_an_empty_value)
{
    results.Record("phase").Add("histogram", infra::MemoryRange<const uint32_t>());

    EXPECT_EQ("\r\nEVT phase id=1 histogram=\r\n", Output());
}

TEST_F(ResultLinesTest, inside_a_command_a_record_needs_no_new_line_first)
{
    response.BeginCommand();
    results.Record("bench").Add("n", 1);
    response.EndCommand();

    EXPECT_EQ("EVT bench id=1 n=1\r\n", Output());
}

TEST_F(ResultLinesTest, a_pair_that_fills_a_line_to_its_limit_stays_on_it)
{
    auto filler = Filler<237>();

    results.Record("phase").Add("k", filler);

    infra::BoundedString::WithStorage<300> expected("\r\nEVT phase id=1 k=");
    expected.append(filler).append("\r\n");
    EXPECT_EQ(expected, Output());
}

TEST_F(ResultLinesTest, a_later_pair_that_fills_a_line_to_its_limit_stays_on_it)
{
    auto filler = Filler<233>();

    results.Record("phase").Add("a", 1).Add("k", filler);

    infra::BoundedString::WithStorage<300> expected("\r\nEVT phase id=1 a=1 k=");
    expected.append(filler).append("\r\n");
    EXPECT_EQ(expected, Output());
}

TEST_F(ResultLinesTest, a_pair_beyond_the_limit_continues_the_record_on_a_line_with_the_same_id)
{
    auto filler = Filler<234>();

    results.Record("phase").Add("a", 1).Add("k", filler);

    infra::BoundedString::WithStorage<300> expected("\r\nEVT phase id=1 a=1\r\n\r\nEVT phase id=1 k=");
    expected.append(filler).append("\r\n");
    EXPECT_EQ(expected, Output());
}

TEST_F(ResultLinesTest, the_digits_of_the_id_count_in_the_length_of_a_line)
{
    for (int record = 1; record != 10; ++record)
        results.Record("bench");
    Output();
    auto filler = Filler<233>();

    results.Record("phase").Add("a", 1).Add("k", filler);

    infra::BoundedString::WithStorage<300> expected("\r\nEVT phase id=10 a=1\r\n\r\nEVT phase id=10 k=");
    expected.append(filler).append("\r\n");
    EXPECT_EQ(expected, Output());
}

TEST_F(ResultLinesTest, a_pair_longer_than_a_line_aborts)
{
    auto filler = Filler<238>();

    EXPECT_DEATH(results.Record("phase").Add("k", filler), "");
}

TEST_F(ResultLinesTest, a_key_or_value_with_a_space_aborts)
{
    EXPECT_DEATH(results.Record("phase").Add("k", "a b"), "");
    EXPECT_DEATH(results.Record("phase").Add("a b", "c"), "");
    EXPECT_DEATH(results.Record("phase").Add("a=b", "c"), "");
}

TEST_F(ResultLinesTest, a_list_fills_a_line_to_its_limit_and_continues_under_the_same_key)
{
    std::array<uint32_t, 40> buckets{};
    buckets.fill(123456789);

    results.Record("phase").Add("histogram", infra::MakeRange(buckets));

    infra::BoundedString::WithStorage<600> expected("\r\nEVT phase id=1 histogram=123456789");
    for (int value = 1; value != 23; ++value)
        expected.append(",123456789");
    expected.append("\r\n\r\nEVT phase id=1 histogram=123456789");
    for (int value = 1; value != 17; ++value)
        expected.append(",123456789");
    expected.append("\r\n");
    EXPECT_EQ(expected, Output());
}

TEST_F(ResultLinesTest, a_list_whose_first_value_does_not_fit_starts_on_the_next_line_with_its_key)
{
    auto filler = Filler<232>();
    const std::array<uint32_t, 1> buckets{ { 123456789 } };

    results.Record("phase").Add("k", filler).Add("h", infra::MakeRange(buckets));

    infra::BoundedString::WithStorage<300> expected("\r\nEVT phase id=1 k=");
    expected.append(filler).append("\r\n\r\nEVT phase id=1 h=123456789\r\n");
    EXPECT_EQ(expected, Output());
}

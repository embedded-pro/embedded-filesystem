#include "validation/firmware/results/ResultLines.hpp"
#include "infra/stream/StringOutputStream.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <limits>

namespace validation
{
    namespace
    {
        constexpr const char* eventHead = "EVT";

        using Number = infra::BoundedString::WithStorage<std::numeric_limits<uint64_t>::digits10 + 1>;

        Number Format(uint64_t value)
        {
            Number text;
            infra::StringOutputStream stream(text);
            stream << value;
            return text;
        }
    }

    ResultRecord::ResultRecord(services::HilResponse& response, const char* kind, uint32_t id)
        : response(response)
        , kind(kind)
        , id(id)
    {
        StartLine();
    }

    ResultRecord& ResultRecord::Add(infra::BoundedConstString key, uint64_t value)
    {
        return Add(key, Format(value));
    }

    ResultRecord& ResultRecord::Add(infra::BoundedConstString key, infra::BoundedConstString value)
    {
        really_assert(key.find_first_of(" =") == infra::BoundedConstString::npos && value.find(' ') == infra::BoundedConstString::npos);

        MakeRoomFor(key.size() + value.size() + 2);
        WriteKey(key);
        Write(value);
        return *this;
    }

    ResultRecord& ResultRecord::Add(infra::BoundedConstString key, infra::MemoryRange<const uint32_t> values)
    {
        if (values.empty())
            return Add(key, infra::BoundedConstString());

        Add(key, Format(values.front()));

        for (auto value : infra::DiscardHead(values, 1))
        {
            auto text = Format(value);

            if (length + text.size() + 1 > ResultLines::maximumLine)
            {
                StartLine();
                Add(key, text);
            }
            else
            {
                Write(",");
                Write(text);
            }
        }

        return *this;
    }

    void ResultRecord::StartLine()
    {
        line.reset();
        line.emplace(response, eventHead, kind);
        length = infra::BoundedConstString(eventHead).size() + 1 + infra::BoundedConstString(kind).size();
        Write(" id=");
        Write(Format(id));
        headerLength = length;
    }

    void ResultRecord::MakeRoomFor(std::size_t size)
    {
        if (length != headerLength && length + size > ResultLines::maximumLine)
            StartLine();

        really_assert(length + size <= ResultLines::maximumLine);
    }

    void ResultRecord::Write(infra::BoundedConstString text)
    {
        *line << text;
        length += text.size();
    }

    void ResultRecord::WriteKey(infra::BoundedConstString key)
    {
        Write(" ");
        Write(key);
        Write("=");
    }

    ResultLines::ResultLines(services::HilResponse& response)
        : response(response)
    {}

    ResultRecord ResultLines::Record(const char* kind)
    {
        return ResultRecord(response, kind, nextId++);
    }
}

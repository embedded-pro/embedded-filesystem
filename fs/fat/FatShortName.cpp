#include "fs/fat/FatShortName.hpp"
#include "fs/fat/CodePage437.hpp"
#include "fs/fat/Utf8.hpp"
#include "infra/util/ReallyAssert.hpp"

namespace fs
{
    namespace
    {
        constexpr std::size_t baseBytes = 8;
        constexpr std::size_t nameBytes = 11;
        constexpr uint8_t kanjiLead = 0x05;
        constexpr uint8_t deleted = 0xe5;

        std::size_t TrimmedLength(const ShortName& name, std::size_t begin, std::size_t end)
        {
            while (end != begin && name.bytes[end - 1] == ' ')
                --end;

            return end - begin;
        }

        uint8_t Display(uint8_t byte, bool lowerCase)
        {
            if (lowerCase && byte >= 'A' && byte <= 'Z')
                return static_cast<uint8_t>(byte - 'A' + 'a');

            return byte;
        }

        std::size_t AppendBytes(const ShortName& name, std::size_t begin, std::size_t length, bool lowerCase, infra::ByteRange out, std::size_t written)
        {
            for (auto index = begin; index != begin + length; ++index)
            {
                auto byte = index == 0 && name.bytes[0] == kanjiLead ? deleted : name.bytes[index];
                auto codePoint = FromCodePage437(Display(byte, lowerCase));
                EncodeUtf8(codePoint, infra::DiscardHead(out, written));
                written += Utf8Bytes(codePoint);
            }

            return written;
        }
    }

    uint8_t LongNameChecksum(const std::array<uint8_t, 11>& name)
    {
        uint8_t sum = 0;

        for (auto byte : name)
            sum = static_cast<uint8_t>(((sum & 1) << 7) + (sum >> 1) + byte);

        return sum;
    }

    std::size_t ShortNameToUtf8(const ShortName& name, infra::ByteRange out)
    {
        really_assert(out.size() >= ShortName::maxUtf8Bytes);

        auto baseLength = TrimmedLength(name, 0, baseBytes);
        auto extensionLength = TrimmedLength(name, baseBytes, nameBytes);
        auto written = AppendBytes(name, 0, baseLength, (name.caseFlags & ShortName::lowerCaseBase) != 0, out, 0);

        if (extensionLength == 0)
            return written;

        out[written++] = '.';
        return AppendBytes(name, baseBytes, extensionLength, (name.caseFlags & ShortName::lowerCaseExtension) != 0, out, written);
    }
}

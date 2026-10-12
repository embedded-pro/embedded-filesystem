#include "fs/fat/Utf8.hpp"
#include "infra/util/ReallyAssert.hpp"
#include <array>
#include <utility>

namespace fs
{
    namespace
    {
        constexpr char32_t largestCodePoint = 0x10ffff;
        constexpr char32_t firstSurrogate = 0xd800;
        constexpr char32_t lastSurrogate = 0xdfff;
        constexpr char32_t firstSupplementary = 0x10000;
        constexpr char16_t firstLowSurrogate = 0xdc00;
        constexpr uint8_t continuationMask = 0xc0;
        constexpr uint8_t continuation = 0x80;
        constexpr std::array<uint8_t, 5> leads{ 0x00, 0x00, 0xc0, 0xe0, 0xf0 };

        struct Lead
        {
            uint8_t bytes;
            char32_t bits;
            char32_t smallest;
        };

        std::optional<Lead> LeadOf(uint8_t byte)
        {
            if (byte < 0x80)
                return Lead{ 1, byte, 0 };
            if ((byte & 0xe0) == 0xc0)
                return Lead{ 2, byte & 0x1fu, 0x80 };
            if ((byte & 0xf0) == 0xe0)
                return Lead{ 3, byte & 0x0fu, 0x800 };
            if ((byte & 0xf8) == 0xf0)
                return Lead{ 4, byte & 0x07u, firstSupplementary };

            return std::nullopt;
        }

        bool IsSurrogate(char32_t codePoint)
        {
            return codePoint >= firstSurrogate && codePoint <= lastSurrogate;
        }

        uint8_t Byte(infra::BoundedConstString text, std::size_t index)
        {
            return static_cast<uint8_t>(text[index]);
        }
    }

    std::optional<DecodedCodePoint> DecodeUtf8(infra::BoundedConstString text, std::size_t offset)
    {
        really_assert(offset < text.size());

        auto lead = LeadOf(Byte(text, offset));

        if (!lead || offset + lead->bytes > text.size())
            return std::nullopt;

        auto codePoint = lead->bits;

        for (std::size_t index = 1; index != lead->bytes; ++index)
        {
            auto byte = Byte(text, offset + index);

            if ((byte & continuationMask) != continuation)
                return std::nullopt;

            codePoint = (codePoint << 6) | (byte & 0x3fu);
        }

        if (codePoint < lead->smallest || codePoint > largestCodePoint || IsSurrogate(codePoint))
            return std::nullopt;

        return DecodedCodePoint{ codePoint, lead->bytes };
    }

    uint8_t Utf8Bytes(char32_t codePoint)
    {
        if (codePoint < 0x80)
            return 1;
        if (codePoint < 0x800)
            return 2;
        if (codePoint < firstSupplementary)
            return 3;

        return 4;
    }

    void EncodeUtf8(char32_t codePoint, infra::ByteRange out)
    {
        auto bytes = Utf8Bytes(codePoint);
        really_assert(out.size() >= bytes);

        for (auto index = bytes - 1; index != 0; --index)
        {
            out[index] = static_cast<uint8_t>(continuation | (codePoint & 0x3fu));
            codePoint >>= 6;
        }

        out[0] = static_cast<uint8_t>(leads[bytes] | codePoint);
    }

    std::optional<std::size_t> Utf16Units(infra::BoundedConstString text)
    {
        std::size_t units = 0;

        for (std::size_t offset = 0; offset != text.size();)
        {
            auto decoded = DecodeUtf8(text, offset);

            if (!decoded)
                return std::nullopt;

            units += decoded->codePoint < firstSupplementary ? 1 : 2;
            offset += decoded->bytes;
        }

        return units;
    }

    bool IsHighSurrogate(char16_t unit)
    {
        return unit >= firstSurrogate && unit < firstLowSurrogate;
    }

    bool IsLowSurrogate(char16_t unit)
    {
        return unit >= firstLowSurrogate && unit <= lastSurrogate;
    }

    char32_t CombineSurrogates(char16_t high, char16_t low)
    {
        really_assert(IsHighSurrogate(high) && IsLowSurrogate(low));

        return firstSupplementary + ((char32_t{ high } - firstSurrogate) << 10) + (char32_t{ low } - char32_t{ firstLowSurrogate });
    }

    Utf16Encoder::Utf16Encoder(infra::BoundedConstString text)
        : text(text)
    {}

    std::optional<char16_t> Utf16Encoder::Next()
    {
        if (pendingLow)
            return std::exchange(pendingLow, std::nullopt);

        if (offset == text.size())
            return std::nullopt;

        auto decoded = DecodeUtf8(text, offset);
        really_assert(decoded.has_value());
        offset += decoded->bytes;

        if (decoded->codePoint < firstSupplementary)
            return static_cast<char16_t>(decoded->codePoint);

        auto supplementary = decoded->codePoint - firstSupplementary;
        pendingLow = static_cast<char16_t>(firstLowSurrogate + (supplementary & 0x3ffu));
        return static_cast<char16_t>(firstSurrogate + (supplementary >> 10));
    }
}

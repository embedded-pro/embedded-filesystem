#ifndef FS_FAT_UTF8_HPP
#define FS_FAT_UTF8_HPP

#include "infra/util/BoundedString.hpp"
#include "infra/util/ByteRange.hpp"
#include <cstddef>
#include <cstdint>
#include <optional>

namespace fs
{
    struct DecodedCodePoint
    {
        char32_t codePoint{ 0 };
        uint8_t bytes{ 0 };

        bool operator==(const DecodedCodePoint& other) const = default;
    };

    std::optional<DecodedCodePoint> DecodeUtf8(infra::BoundedConstString text, std::size_t offset);
    uint8_t Utf8Bytes(char32_t codePoint);
    void EncodeUtf8(char32_t codePoint, infra::ByteRange out);
    std::optional<std::size_t> Utf16Units(infra::BoundedConstString text);

    bool IsHighSurrogate(char16_t unit);
    bool IsLowSurrogate(char16_t unit);
    char32_t CombineSurrogates(char16_t high, char16_t low);

    class Utf16Encoder
    {
    public:
        explicit Utf16Encoder(infra::BoundedConstString text);

        std::optional<char16_t> Next();

    private:
        infra::BoundedConstString text;
        std::size_t offset{ 0 };
        std::optional<char16_t> pendingLow;
    };
}

#endif

#include "fs/fat/FatName.hpp"
#include "fs/fat/Utf8.hpp"
#include <algorithm>
#include <string_view>

namespace fs
{
    namespace
    {
        constexpr std::size_t maxUnits = 255;
        constexpr std::string_view reserved{ "\"*/:<>?\\|" };

        bool IsReserved(char32_t codePoint)
        {
            return codePoint < 0x20 || codePoint == 0x7f || (codePoint < 0x80 && reserved.find(static_cast<char>(codePoint)) != std::string_view::npos);
        }

        bool HasReservedCharacter(infra::BoundedConstString name)
        {
            for (std::size_t offset = 0; offset != name.size();)
            {
                auto decoded = DecodeUtf8(name, offset);

                if (IsReserved(decoded->codePoint))
                    return true;

                offset += decoded->bytes;
            }

            return false;
        }
    }

    Result ValidateFatName(infra::BoundedConstString name)
    {
        if (name.empty() || name.back() == '.' || name.back() == ' ')
            return Result::invalidPath;

        auto units = Utf16Units(name);

        if (!units || HasReservedCharacter(name))
            return Result::invalidPath;

        if (*units > maxUnits)
            return Result::nameTooLong;

        return Result::success;
    }

    char FoldAscii(char character)
    {
        if (character >= 'a' && character <= 'z')
            return static_cast<char>(character - 'a' + 'A');

        return character;
    }

    bool EqualIgnoringAsciiCase(infra::BoundedConstString x, infra::BoundedConstString y)
    {
        return std::ranges::equal(x, y, [](char a, char b)
            {
                return FoldAscii(a) == FoldAscii(b);
            });
    }
}

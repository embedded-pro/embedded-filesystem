#include "fs/fat/FatShortNameGeneration.hpp"
#include "fs/fat/Utf8.hpp"
#include "infra/util/CrcCcittCalculator.hpp"
#include <algorithm>
#include <string_view>

namespace fs
{
    namespace
    {
        constexpr std::size_t baseBytes = 8;
        constexpr std::size_t extensionBytes = 3;
        constexpr std::size_t hashedBytes = 2;
        constexpr std::string_view notInShortNames{ "\"*/:<>?\\|+,;=[]. " };
        constexpr std::string_view hexadecimal{ "0123456789ABCDEF" };

        enum class Case : uint8_t
        {
            none,
            lower,
            upper,
            mixed
        };

        bool IsLower(char character)
        {
            return character >= 'a' && character <= 'z';
        }

        bool IsUpper(char character)
        {
            return character >= 'A' && character <= 'Z';
        }

        bool FitsShortName(char32_t codePoint)
        {
            return codePoint > 0x20 && codePoint < 0x7f && notInShortNames.find(static_cast<char>(codePoint)) == std::string_view::npos;
        }

        uint8_t Upper(char character)
        {
            return static_cast<uint8_t>(IsLower(character) ? character - 'a' + 'A' : character);
        }

        Case CaseOf(std::string_view part)
        {
            auto lower = std::ranges::any_of(part, IsLower);
            auto upper = std::ranges::any_of(part, IsUpper);

            if (lower && upper)
                return Case::mixed;

            if (lower)
                return Case::lower;

            return upper ? Case::upper : Case::none;
        }

        bool FitsPart(std::string_view part, std::size_t limit)
        {
            return part.size() <= limit && std::ranges::all_of(part, [](char character)
                                               {
                                                   return FitsShortName(static_cast<uint8_t>(character));
                                               });
        }

        std::size_t TrimmedBase(const ShortNameBytes& name)
        {
            std::size_t length = baseBytes;

            while (length != 0 && name[length - 1] == ' ')
                --length;

            return length;
        }

        std::optional<std::size_t> LastDot(infra::BoundedConstString name)
        {
            std::optional<std::size_t> lastDot;
            bool content = false;

            for (std::size_t offset = 0; offset != name.size(); ++offset)
                if (name[offset] == '.' && content)
                    lastDot = offset;
                else if (name[offset] != '.' && name[offset] != ' ')
                    content = true;

            return lastDot;
        }

        void AppendBasis(infra::BoundedConstString name, std::size_t begin, std::size_t end, infra::ByteRange out)
        {
            std::size_t written = 0;

            for (auto offset = begin; offset != end && written != out.size();)
            {
                auto decoded = DecodeUtf8(name, offset);
                offset += decoded->bytes;

                if (decoded->codePoint != ' ' && decoded->codePoint != '.')
                    out[written++] = FitsShortName(decoded->codePoint) ? Upper(static_cast<char>(decoded->codePoint)) : '_';
            }
        }

        void ReplaceEndOfBase(ShortNameBytes& name, std::string_view replacement, std::size_t keep)
        {
            auto at = std::min(keep, baseBytes - replacement.size());
            std::ranges::copy(replacement, name.begin() + at);
            std::fill(name.begin() + at + replacement.size(), name.begin() + baseBytes, ' ');
        }
    }

    std::optional<ShortName> ExactShortName(infra::BoundedConstString name)
    {
        auto bytes = ShortFormOf(name);

        if (!bytes)
            return std::nullopt;

        std::string_view view{ name.data(), name.size() };
        auto dot = std::min(view.find('.'), view.size());
        auto baseCase = CaseOf(view.substr(0, dot));
        auto extensionCase = CaseOf(view.substr(dot));

        if (baseCase == Case::mixed || extensionCase == Case::mixed)
            return std::nullopt;

        ShortName exact{ *bytes };

        if (baseCase == Case::lower)
            exact.caseFlags |= ShortName::lowerCaseBase;

        if (extensionCase == Case::lower)
            exact.caseFlags |= ShortName::lowerCaseExtension;

        return exact;
    }

    std::optional<ShortNameBytes> ShortFormOf(infra::BoundedConstString name)
    {
        std::string_view view{ name.data(), name.size() };
        auto dot = view.find('.');
        auto base = view.substr(0, dot);
        auto extension = dot == std::string_view::npos ? std::string_view{} : view.substr(dot + 1);

        if (base.empty() || !FitsPart(base, baseBytes) || !FitsPart(extension, extensionBytes) || (dot != std::string_view::npos && extension.empty()))
            return std::nullopt;

        ShortNameBytes bytes;
        bytes.fill(' ');
        std::ranges::transform(base, bytes.begin(), Upper);
        std::ranges::transform(extension, bytes.begin() + baseBytes, Upper);
        return bytes;
    }

    ShortNameBytes BasisName(infra::BoundedConstString name)
    {
        ShortNameBytes basis;
        basis.fill(' ');
        auto lastDot = LastDot(name);

        AppendBasis(name, 0, lastDot.value_or(name.size()), infra::Head(infra::MakeRange(basis), baseBytes));

        if (lastDot)
            AppendBasis(name, *lastDot + 1, name.size(), infra::DiscardHead(infra::MakeRange(basis), baseBytes));

        return basis;
    }

    ShortNameBytes WithNumericTail(const ShortNameBytes& basis, uint32_t tail)
    {
        std::array<char, baseBytes> text{};
        auto end = text.end();

        do
        {
            *--end = static_cast<char>('0' + tail % 10);
            tail /= 10;
        } while (tail != 0);

        *--end = '~';

        auto name = basis;
        ReplaceEndOfBase(name, std::string_view(&*end, static_cast<std::size_t>(text.end() - end)), TrimmedBase(basis));
        return name;
    }

    std::optional<uint32_t> NumericTailOf(const ShortNameBytes& name, const ShortNameBytes& basis)
    {
        std::string_view base{ reinterpret_cast<const char*>(name.data()), TrimmedBase(name) };
        auto tilde = base.rfind('~');

        if (tilde == std::string_view::npos || tilde + 1 == base.size())
            return std::nullopt;

        uint32_t tail = 0;

        for (auto digit : base.substr(tilde + 1))
        {
            if (digit < '0' || digit > '9')
                return std::nullopt;

            tail = tail * 10 + static_cast<uint32_t>(digit - '0');
        }

        if (tail == 0 || WithNumericTail(basis, tail) != name)
            return std::nullopt;

        return tail;
    }

    ShortNameBytes HashedAlias(const ShortNameBytes& basis, infra::BoundedConstString name)
    {
        infra::CrcCcittCalculator crc;
        crc.Update(infra::StringAsByteRange(name));
        auto value = crc.Result();

        std::array<char, 6> text{ hexadecimal[(value >> 12) & 0xf], hexadecimal[(value >> 8) & 0xf], hexadecimal[(value >> 4) & 0xf], hexadecimal[value & 0xf], '~', '1' };
        auto alias = basis;
        ReplaceEndOfBase(alias, std::string_view(text.data(), text.size()), std::min(TrimmedBase(basis), hashedBytes));
        return alias;
    }
}

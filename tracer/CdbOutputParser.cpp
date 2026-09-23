#include "tracer/CdbOutputParser.hpp"
#include "tracer/ArmPlan.hpp"
#include <algorithm>
#include <cctype>
#include <cstddef>

namespace appbox::tracer
{
namespace
{

/** Text which introduces a module load line. */
constexpr const char* kModuleLoadPrefix = "ModLoad:";

/**
 * @brief Report whether a character separates tokens of the debugger output.
 *
 * @param[in] character Character to test.
 * @return Whether the character is whitespace or the end of a string.
 */
bool IsSeparator(char character)
{
    return character == ' ' || character == '\t' || character == '\r' || character == '\n' ||
           character == '\0';
}

/**
 * @brief Report whether a character is a decimal digit.
 *
 * @param[in] character Character to test.
 * @return Whether the character is a digit.
 */
bool IsDigit(char character)
{
    return character >= '0' && character <= '9';
}

/**
 * @brief Convert a hexadecimal character into its value.
 *
 * @param[in] character Character to convert.
 * @return The value, or -1 when the character is not a hexadecimal digit.
 */
int HexValue(char character)
{
    if (character >= '0' && character <= '9')
    {
        return character - '0';
    }

    if (character >= 'a' && character <= 'f')
    {
        return character - 'a' + 10;
    }

    if (character >= 'A' && character <= 'F')
    {
        return character - 'A' + 10;
    }

    return -1;
}

/**
 * @brief Parse a full address token of a module load line.
 *
 * The token has to consist of hexadecimal digits with an optional backtick
 * between the two halves of a 64 bit address, and it has to carry at least
 * eight digits. The stricter check keeps a single address which is followed by
 * a path from being read as two addresses (a drive letter such as `C` is a
 * hexadecimal digit as well).
 *
 * @param[in] text Text to parse.
 * @param[in,out] position Index of the first character; moved behind the token.
 * @param[out] value Parsed value.
 * @return Whether a full address token was parsed.
 */
bool ParseAddressToken(const std::string& text, std::size_t& position, std::uint64_t& value)
{
    const std::size_t start = position;
    std::size_t cursor = position;
    std::size_t digits = 0;
    std::size_t separators = 0;
    std::uint64_t result = 0;

    while (cursor < text.size() && !IsSeparator(text[cursor]))
    {
        const char character = text[cursor];
        if (character == '`')
        {
            ++separators;
            ++cursor;
            continue;
        }

        const int digit = HexValue(character);
        if (digit < 0)
        {
            /* Not an address token at all. */
            return false;
        }

        result = (result << 4U) | static_cast<std::uint64_t>(digit);
        ++digits;
        ++cursor;
    }

    if (digits < 8U || separators > 1U || cursor == start)
    {
        return false;
    }

    value = result;
    position = cursor;
    return true;
}

/**
 * @brief Find the next debugger prompt in a buffer.
 *
 * A prompt is `process:thread>` (`0:000>`), optionally followed by the processor
 * architecture of a WOW64 process (`0:000:x86>`). It starts a token, so it is
 * preceded by the beginning of the buffer or by whitespace.
 *
 * @param[in] buffer Text to search.
 * @param[out] position Index of the prompt.
 * @param[out] length Length of the prompt text.
 * @param[out] session Process index of the prompt.
 * @return Whether a prompt was found.
 */
bool FindPrompt(const std::string& buffer, std::size_t& position, std::size_t& length,
                std::uint32_t& session)
{
    for (std::size_t index = 0; index < buffer.size(); ++index)
    {
        if (!IsDigit(buffer[index]))
        {
            continue;
        }

        if (index > 0 && !IsSeparator(buffer[index - 1]))
        {
            continue;
        }

        std::size_t cursor = index;
        std::uint64_t process = 0;
        while (cursor < buffer.size() && IsDigit(buffer[cursor]))
        {
            process = process * 10U + static_cast<std::uint64_t>(buffer[cursor] - '0');
            ++cursor;
        }

        if (cursor >= buffer.size() || buffer[cursor] != ':')
        {
            continue;
        }

        ++cursor;
        std::size_t thread_digits = 0;
        while (cursor < buffer.size() && HexValue(buffer[cursor]) >= 0)
        {
            ++cursor;
            ++thread_digits;
        }

        if (thread_digits < 3U || thread_digits > 4U)
        {
            continue;
        }

        /* Optional processor architecture, e.g. "0:000:x86>". */
        if (cursor < buffer.size() && buffer[cursor] == ':')
        {
            const std::size_t architecture_start = ++cursor;
            while (cursor < buffer.size() &&
                   (std::isalnum(static_cast<unsigned char>(buffer[cursor])) != 0))
            {
                ++cursor;
            }

            if (cursor == architecture_start)
            {
                continue;
            }
        }

        if (cursor >= buffer.size() || buffer[cursor] != '>')
        {
            continue;
        }

        ++cursor;
        position = index;
        length = cursor - index;
        session = static_cast<std::uint32_t>(process);
        return true;
    }

    return false;
}

/**
 * @brief Remove spaces, tabs and carriage returns around a text.
 *
 * @param[in] text Text to trim.
 * @return The trimmed text.
 */
std::string Trim(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos)
    {
        return {};
    }

    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1U);
}

/**
 * @brief Widen a byte string.
 *
 * The decoded output only contains ASCII identifiers and paths; a byte which is
 * not ASCII is kept as it is instead of failing.
 *
 * @param[in] text Text to widen.
 * @return The wide text.
 */
std::wstring Widen(const std::string& text)
{
    std::wstring result;
    result.reserve(text.size());
    for (const char character : text)
    {
        result.push_back(static_cast<wchar_t>(static_cast<unsigned char>(character)));
    }

    return result;
}

/**
 * @brief Decode one chunk of output which sits between two prompts or newlines.
 *
 * @param[in] text Chunk to decode.
 * @param[in,out] events Events to append to.
 */
void DecodeChunk(const std::string& text, std::vector<CdbEvent>& events)
{
    const std::string trimmed = Trim(text);
    if (trimmed.empty())
    {
        return;
    }

    /* A module load line carries the base address of a module, which is what
     * the breakpoint addresses of the session are computed from. */
    if (trimmed.compare(0, std::char_traits<char>::length(kModuleLoadPrefix), kModuleLoadPrefix) == 0)
    {
        std::size_t cursor = std::char_traits<char>::length(kModuleLoadPrefix);
        while (cursor < trimmed.size() && IsSeparator(trimmed[cursor]))
        {
            ++cursor;
        }

        std::uint64_t base = 0;
        if (ParseAddressToken(trimmed, cursor, base) && cursor < trimmed.size() &&
            IsSeparator(trimmed[cursor]))
        {
            ++cursor;
            while (cursor < trimmed.size() && IsSeparator(trimmed[cursor]))
            {
                ++cursor;
            }

            std::uint64_t end = 0;
            if (ParseAddressToken(trimmed, cursor, end))
            {
                while (cursor < trimmed.size() && IsSeparator(trimmed[cursor]))
                {
                    ++cursor;
                }

                CdbEvent event;
                event.kind = CdbEvent::Kind::ModuleLoad;
                event.image_base = base;
                event.image_path = Widen(Trim(trimmed.substr(cursor)));
                events.push_back(std::move(event));
            }
        }

        return;
    }

    /* A marker line reports that one armed address was entered. */
    const std::size_t marker = trimmed.find(kHitMarker);
    if (marker == std::string::npos)
    {
        return;
    }

    CdbEvent event;
    event.kind = CdbEvent::Kind::Hit;

    std::size_t cursor = marker + std::char_traits<char>::length(kHitMarker);
    while (cursor < trimmed.size())
    {
        while (cursor < trimmed.size() && IsSeparator(trimmed[cursor]))
        {
            ++cursor;
        }

        const std::size_t start = cursor;
        while (cursor < trimmed.size() && !IsSeparator(trimmed[cursor]))
        {
            ++cursor;
        }

        if (cursor == start)
        {
            break;
        }

        /* Only a plain `module!function` token is accepted: anything else is a
         * garbled line and must not end up in the report. */
        const std::string token = trimmed.substr(start, cursor - start);
        if (token.find('!') != std::string::npos && token.find_first_of("\";'") == std::string::npos)
        {
            event.names.push_back(Widen(token));
        }
    }

    if (!event.names.empty())
    {
        events.push_back(std::move(event));
    }
}

} // namespace

void CdbOutputParser::Feed(std::string_view chunk, std::vector<CdbEvent>& events)
{
    pending_.append(chunk.data(), chunk.size());

    for (;;)
    {
        std::size_t prompt_position = 0;
        std::size_t prompt_length = 0;
        std::uint32_t session = 0;
        const bool has_prompt = FindPrompt(pending_, prompt_position, prompt_length, session);
        const std::size_t newline = pending_.find('\n');

        if (has_prompt && (newline == std::string::npos || prompt_position < newline))
        {
            DecodeChunk(pending_.substr(0, prompt_position), events);
            pending_.erase(0, prompt_position + prompt_length);

            CdbEvent event;
            event.kind = CdbEvent::Kind::Prompt;
            event.session = session;
            events.push_back(event);
            continue;
        }

        if (newline != std::string::npos)
        {
            DecodeChunk(pending_.substr(0, newline), events);
            pending_.erase(0, newline + 1U);
            continue;
        }

        /* The rest is an incomplete line; it is decoded with the next chunk. */
        break;
    }

    /* Keep the buffer bounded: the tail of an incomplete line is short, but a
     * long line without a prompt must not grow without a limit. */
    constexpr std::size_t kMaxPending = 64U * 1024U;
    if (pending_.size() > kMaxPending)
    {
        pending_.erase(0, pending_.size() - kMaxPending);
    }
}

} // namespace appbox::tracer

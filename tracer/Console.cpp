#include "tracer/Console.hpp"
#include "WString.hpp"
#include <windows.h>

namespace appbox::tracer
{
namespace
{

/**
 * @brief Write a wide string to a standard handle.
 *
 * The handle is inspected first: a console accepts wide characters directly,
 * every other stream (a pipe or a file, i.e. a redirected standard output)
 * receives the UTF-8 encoding of the text.
 *
 * @param[in] handle Standard handle to write to.
 * @param[in] text Text to write.
 */
void WriteToHandle(HANDLE handle, const std::wstring& text)
{
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE || text.empty())
    {
        return;
    }

    DWORD console_mode = 0;
    if (::GetConsoleMode(handle, &console_mode) != 0)
    {
        DWORD written = 0;
        ::WriteConsoleW(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
        return;
    }

    const std::string utf8 = appbox::WideToUTF8(text);
    DWORD written = 0;
    ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
}

} // namespace

void WriteStdout(const std::wstring& text)
{
    WriteToHandle(::GetStdHandle(STD_OUTPUT_HANDLE), text);
}

void WriteStderr(const std::wstring& text)
{
    WriteToHandle(::GetStdHandle(STD_ERROR_HANDLE), text);
}

std::wstring DecodeConsoleBytes(const std::string& bytes)
{
    if (bytes.empty())
    {
        return {};
    }

    const int size = ::MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()),
                                           nullptr, 0);
    if (size <= 0)
    {
        return {};
    }

    std::wstring text(static_cast<std::size_t>(size), L'\0');
    if (::MultiByteToWideChar(CP_ACP, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(),
                              size) != size)
    {
        return {};
    }

    return text;
}

} // namespace appbox::tracer

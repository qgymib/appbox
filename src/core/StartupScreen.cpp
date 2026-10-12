#include "StartupScreen.hpp"
#include "SandboxLayout.hpp"
#include <algorithm>
#include <filesystem>
#include <system_error>

namespace
{

/**
 * @brief The extensions a startup image may carry, in the order of the dialog.
 *
 * The list is spelled once: `StartupImageExtensions()` hands it out, the
 * validation of a session and the name of the archive entry are derived from
 * it, and the file dialog of the Settings workspace offers it.
 */
constexpr const wchar_t* kStartupImageExtensions[] = { L".png", L".jpg", L".jpeg", L".bmp" };

/**
 * @brief Lower the ASCII letters of a text.
 *
 * The extensions of the archive layout are ASCII, so the fold is done by hand
 * instead of through a locale aware conversion.
 *
 * @param[in] text Text to fold.
 * @return The text with every upper case letter lowered.
 */
std::wstring ToLowerAscii(const std::wstring& text)
{
    std::wstring lowered = text;
    for (auto& character : lowered)
    {
        if (character >= L'A' && character <= L'Z')
        {
            character = static_cast<wchar_t>(character - L'A' + L'a');
        }
    }

    return lowered;
}

/**
 * @brief Narrow a text which is known to be ASCII.
 *
 * The entry names of the archive layout and the extensions of a startup image
 * are ASCII, so the narrow spelling of such a text is its wide spelling. The
 * conversion is explicit instead of implicit, which keeps the narrowing out of
 * the code paths which may carry text of the user.
 *
 * @param[in] text Text to narrow.
 * @return The narrow spelling of the text.
 */
std::string NarrowAscii(const std::wstring& text)
{
    std::string narrowed;
    narrowed.reserve(text.size());
    for (const auto character : text)
    {
        narrowed.push_back(static_cast<char>(character));
    }

    return narrowed;
}

} // namespace

namespace appbox
{

bool StartupScreen::IsEmpty() const
{
    return enabled && image.empty();
}

const std::vector<std::wstring>& StartupImageExtensions()
{
    static const std::vector<std::wstring> extensions(std::begin(kStartupImageExtensions),
                                                      std::end(kStartupImageExtensions));
    return extensions;
}

std::wstring StartupImageExtension(const std::wstring& path)
{
    /*
     * Only the file name carries the extension: a directory of the path may
     * carry a dot of its own, which must not be taken for it.
     */
    const auto separator = path.find_last_of(L"\\/");
    const auto name_begin = separator == std::wstring::npos ? 0 : separator + 1;

    const auto dot = path.find_last_of(L'.');
    if (dot == std::wstring::npos || dot < name_begin || dot + 1 >= path.size())
    {
        return {};
    }

    return ToLowerAscii(path.substr(dot));
}

bool IsSupportedStartupImage(const std::wstring& path)
{
    const auto extension = StartupImageExtension(path);
    if (extension.empty())
    {
        return false;
    }

    const auto& extensions = StartupImageExtensions();
    return std::find(extensions.begin(), extensions.end(), extension) != extensions.end();
}

bool HasStartupImage(const StartupScreen& screen)
{
    return !screen.image.empty() && IsSupportedStartupImage(screen.image);
}

std::wstring StartupImageFileName(const std::wstring& host_path)
{
    if (!IsSupportedStartupImage(host_path))
    {
        return {};
    }

    return std::wstring(layout::kStartupImageBaseNameW) + StartupImageExtension(host_path);
}

std::string StartupImageEntryName(const std::wstring& host_path)
{
    const auto file_name = StartupImageFileName(host_path);
    if (file_name.empty())
    {
        return {};
    }

    /*
     * Both parts are ASCII (the names of the archive layout and an extension
     * of the list above), so the wide and the narrow spelling are the same and
     * the entry name needs no conversion.
     */
    std::string entry = layout::kStartupDirName;
    entry.push_back('/');
    entry.append(NarrowAscii(file_name));
    return entry;
}

bool ValidateStartupScreen(const StartupScreen& screen, std::string& error)
{
    error.clear();

    if (screen.image.empty())
    {
        /* A session without an image shows the name and the progress only. */
        return true;
    }

    if (!IsSupportedStartupImage(screen.image))
    {
        error = "the startup image has an unsupported format: " + std::filesystem::path(screen.image).u8string();
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_regular_file(screen.image, ec) || ec)
    {
        error = "the startup image was not found: " + std::filesystem::path(screen.image).u8string();
        return false;
    }

    return true;
}

} // namespace appbox

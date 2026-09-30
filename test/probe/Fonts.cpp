#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "utils/ReadFileFull.hpp"
#include "Fonts.hpp"
#include "WString.hpp"
#include <cstring>
#include <string>
#include <vector>

namespace
{

/**
 * @brief Collect the family names the font table of a device context carries.
 * @param[in] logfont Font the enumeration reports.
 * @param[in] metric Metrics of the font, unused.
 * @param[in] type Type of the font, unused.
 * @param[in] param Vector of names.
 * @return 1, which keeps the enumeration going.
 */
int CALLBACK CollectFamily(const LOGFONTW* logfont, const TEXTMETRICW* /*metric*/, DWORD /*type*/, LPARAM param)
{
    auto* names = reinterpret_cast<std::vector<std::wstring>*>(param);
    names->emplace_back(logfont->lfFaceName);
    return 1;
}

/**
 * @brief Get the family names the font table of this process carries.
 * @return The names.
 */
std::vector<std::wstring> EnumerateFamilies()
{
    std::vector<std::wstring> names;

    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
    {
        return names;
    }

    LOGFONTW logfont = {};
    logfont.lfCharSet = DEFAULT_CHARSET;

    EnumFontFamiliesExW(dc, &logfont, CollectFamily, reinterpret_cast<LPARAM>(&names), 0);
    DeleteDC(dc);
    return names;
}

/**
 * @brief Create a font for a family and report the face the system chose.
 *
 * The face of a font which was created for a family is the family when the font
 * table carries it, which is what makes the font usable for an application.
 *
 * @param[in] family Family name.
 * @param[out] face Face name the created font reports.
 * @return true when a font was created.
 */
bool CreateFontOfFamily(const std::wstring& family, std::wstring& face)
{
    HDC dc = CreateCompatibleDC(nullptr);
    if (dc == nullptr)
    {
        return false;
    }

    LOGFONTW logfont = {};
    logfont.lfHeight = -12;
    logfont.lfCharSet = DEFAULT_CHARSET;
    wcsncpy_s(logfont.lfFaceName, family.c_str(), _TRUNCATE);

    HFONT font = CreateFontIndirectW(&logfont);
    if (font == nullptr)
    {
        DeleteDC(dc);
        return false;
    }

    HGDIOBJ previous = SelectObject(dc, font);

    wchar_t buffer[LF_FACESIZE] = {};
    GetTextFaceW(dc, LF_FACESIZE, buffer);
    face.assign(buffer);

    SelectObject(dc, previous);
    DeleteObject(font);
    DeleteDC(dc);
    return true;
}

} // namespace

static nlohmann::json ProbeFonts_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolFonts::Req>();

    appbox::test::ProtocolFonts::Rsp rsp;

    const std::wstring family = appbox::UTF8ToWide(req.family);
    const std::wstring absent_family =
        req.absent_family.empty() ? std::wstring() : appbox::UTF8ToWide(req.absent_family);
    const std::wstring view_path = appbox::UTF8ToWide(req.view_path);

    /* The font table of this process carries the family. */
    for (const auto& name : EnumerateFamilies())
    {
        if (CompareStringOrdinal(name.c_str(), -1, family.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            rsp.enumerated = true;
        }

        if (!absent_family.empty() &&
            CompareStringOrdinal(name.c_str(), -1, absent_family.c_str(), -1, TRUE) == CSTR_EQUAL)
        {
            rsp.absent_enumerated = true;
        }
    }

    /* A font which is created for the family is the family itself. */
    std::wstring face;
    if (CreateFontOfFamily(family, face))
    {
        rsp.face = appbox::WideToUTF8(face);
        rsp.created = CompareStringOrdinal(face.c_str(), -1, family.c_str(), -1, TRUE) == CSTR_EQUAL;
    }

    /* The file of the view, which is the file of the layer the packer wrote. */
    std::vector<std::uint8_t> bytes;
    rsp.file_read = appbox::test::ReadFileFull(view_path, bytes) == 0;
    rsp.file_size = bytes.size();

    /*
     * The addition by the view path is the call the hook of the sandbox
     * rewrites: the font driver of the system opens the file without passing
     * the hooks, so a path of the view only reaches the file of the layer when
     * the hook resolved it.
     */
    rsp.add_result = AddFontResourceExW(view_path.c_str(), FR_PRIVATE, nullptr);

    return rsp;
}

appbox::test::Probe appbox::test::ProbeFonts("Fonts", ProbeFonts_Entry);

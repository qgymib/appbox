#include "utils/WinAPI.h" /* Must be first include file */
#include <cwctype>
#include "Sandbox.hpp"
#include "LayerPath.hpp"

/**
 * @brief Case insensitive comparison of a path against a prefix.
 * @param[in] path Path to test.
 * @param[in] prefix Prefix to test against.
 * @return true when \p path starts with \p prefix.
 */
static bool StartsWithI(const std::wstring& path, const std::wstring& prefix)
{
    if (path.size() < prefix.size())
    {
        return false;
    }

    for (size_t i = 0; i < prefix.size(); ++i)
    {
        if (std::towlower(path[i]) != std::towlower(prefix[i]))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief Whether a path is below a prefix, on a component boundary.
 *
 * A layer root which is only a prefix of the path without being a whole
 * component must not match: the root `\AppBox\data` does not hold the entry
 * `\AppBox\database\file.txt`.
 *
 * @param[in] path Path to test.
 * @param[in] prefix Prefix to test against.
 * @return true when \p path is \p prefix or a path below it.
 */
static bool IsBelowI(const std::wstring& path, const std::wstring& prefix)
{
    if (!StartsWithI(path, prefix))
    {
        return false;
    }

    if (path.size() == prefix.size())
    {
        return true;
    }

    if (!prefix.empty() && prefix.back() == L'\\')
    {
        /* The prefix already ends on a component boundary. */
        return true;
    }

    return path[prefix.size()] == L'\\';
}

/**
 * @brief Whether a character is a drive letter.
 * @param[in] ch Character to test.
 * @return true when the character is a drive letter.
 */
static bool IsDriveLetter(wchar_t ch)
{
    ch = static_cast<wchar_t>(std::towupper(ch));
    return ch >= L'A' && ch <= L'Z';
}

/**
 * @brief Namespace which opens a drive in a DOS style NT path.
 * @param[in] dosNtPath Path of the form `\??\C:\dir\file`.
 * @return `\??\`, `\GLOBAL??\` or an empty string when the path has no such
 *         namespace.
 */
static std::wstring DosNtNamespace(const std::wstring& dosNtPath)
{
    if (StartsWithI(dosNtPath, L"\\GLOBAL??\\"))
    {
        return L"\\GLOBAL??\\";
    }
    if (StartsWithI(dosNtPath, L"\\??\\"))
    {
        return L"\\??\\";
    }
    return L"";
}

/**
 * @brief Reduce a DOS style NT path to the form the file system reports.
 *
 * The name of a file object starts at the root of its volume, while the paths
 * of the configuration carry the namespace of the object manager and the drive
 * of the view. The helper removes both, so the two forms can be compared.
 *
 * @param[in] dosNtPath Path of the form `\??\C:\dir\file` or
 *                      `\GLOBAL??\C:\dir\file`.
 * @param[out] volumePath The same path without the namespace and the drive.
 * @return true when the path has that form, false otherwise.
 */
static bool AsVolumePath(const std::wstring& dosNtPath, std::wstring& volumePath)
{
    size_t offset = 0;
    if (StartsWithI(dosNtPath, L"\\??\\"))
    {
        offset = 4;
    }
    else if (StartsWithI(dosNtPath, L"\\GLOBAL??\\"))
    {
        offset = 10;
    }
    else
    {
        return false;
    }

    if (dosNtPath.size() < offset + 3 || !IsDriveLetter(dosNtPath[offset]) || dosNtPath[offset + 1] != L':' ||
        dosNtPath[offset + 2] != L'\\')
    {
        return false;
    }

    /* Keep the separator which opens the first component below the volume. */
    volumePath = dosNtPath.substr(offset + 2);
    return true;
}

/**
 * @brief Undo the rebase of the upper layer.
 *
 * The upper layer rebases a view path by encoding the drive of the view as the
 * first component below its own root, so that component has to be turned back
 * into the drive of the view path.
 *
 * @param[in] layerPath Path of the upper layer, below \p upperRoot.
 * @param[in] upperRoot Root of the upper layer, in the shape of \p layerPath.
 * @param[in] namespace Namespace which opens the drive of a DOS style NT path,
 *                      empty for the form which starts at the volume.
 * @param[in] keepDrive Whether the shape of the path carries the drive of the
 *                      volume.
 * @param[out] viewPath Path of the view, in the shape of \p layerPath.
 * @return true when the path is below the root of the upper layer.
 */
static bool RebaseUpperPathToView(const std::wstring& layerPath, const std::wstring& upperRoot,
                                  const std::wstring& namespacePrefix, bool keepDrive, std::wstring& viewPath)
{
    if (upperRoot.empty() || !IsBelowI(layerPath, upperRoot))
    {
        return false;
    }

    const std::wstring remainder = layerPath.substr(upperRoot.size());
    if (remainder.size() < 2 || remainder[0] != L'\\' || !IsDriveLetter(remainder[1]))
    {
        return false;
    }

    if (remainder.size() > 2 && remainder[2] != L'\\')
    {
        return false;
    }

    /* The path is the root of the drive of the view when it has no component. */
    const std::wstring rest = remainder.size() > 2 ? remainder.substr(2) : L"\\";
    if (!keepDrive)
    {
        viewPath = rest;
        return true;
    }

    const wchar_t drive = static_cast<wchar_t>(std::towupper(remainder[1]));
    viewPath = namespacePrefix + drive + L":" + rest;
    return true;
}

bool appbox::filesystem::RebaseLayerPathToView(const std::wstring& layerPath, std::wstring& viewPath)
{
    if (appbox::sandbox == nullptr || layerPath.empty())
    {
        return false;
    }

    const auto& fs = appbox::sandbox->fs;

    /*
     * The layer path of a DOS style NT path carries the namespace and the drive
     * of the layer, so the roots of the configuration are matched directly.
     */
    if (!DosNtNamespace(layerPath).empty())
    {
        if (RebaseUpperPathToView(layerPath, fs.fs_upper, DosNtNamespace(fs.fs_upper), true, viewPath))
        {
            return true;
        }

        for (const auto& mapping : fs.fs_lower)
        {
            if (mapping.host_nt_path.empty() || !IsBelowI(layerPath, mapping.host_nt_path))
            {
                continue;
            }

            viewPath = mapping.mapped_nt_path + layerPath.substr(mapping.host_nt_path.size());
            return true;
        }

        return false;
    }

    /*
     * The name of a handle starts at the root of its volume, so the same
     * comparison runs on the two paths without their namespace and drive.
     */
    std::wstring upperPath;
    if (AsVolumePath(fs.fs_upper, upperPath) && RebaseUpperPathToView(layerPath, upperPath, L"", false, viewPath))
    {
        return true;
    }

    for (const auto& mapping : fs.fs_lower)
    {
        std::wstring lowerPath;
        std::wstring mappedPath;
        if (!AsVolumePath(mapping.host_nt_path, lowerPath) || !AsVolumePath(mapping.mapped_nt_path, mappedPath))
        {
            continue;
        }
        if (lowerPath.empty() || !IsBelowI(layerPath, lowerPath))
        {
            continue;
        }

        const std::wstring remainder = layerPath.substr(lowerPath.size());
        if (mappedPath == L"\\")
        {
            /* The layer is mounted at the root of the volume of the view. */
            viewPath = remainder.empty() ? L"\\" : remainder;
        }
        else
        {
            viewPath = mappedPath + remainder;
        }
        return true;
    }

    return false;
}

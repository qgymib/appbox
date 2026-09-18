#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
/*
 * The resource API is used with the wide character forms: the RT_ICON and
 * RT_GROUP_ICON macros expand to their ANSI form unless UNICODE is defined,
 * and the targets of the packer do not define it.
 */
#ifndef UNICODE
#define UNICODE
#endif
#include <windows.h>
#include "ApplicationIcon.hpp"
#include "WString.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <system_error>
#include <vector>

namespace
{

/** Offset of the PE signature inside the DOS header of an image. */
constexpr std::size_t kPeSignatureOffset = 0x3C;

/** Size of the group icon header which precedes the icon directory entries. */
constexpr std::size_t kGroupIconHeaderSize = 6;

/** Size of one group icon directory entry. */
constexpr std::size_t kGroupIconEntrySize = 14;

/** First resource id which may be handed out to a copied icon. */
constexpr std::uint32_t kFirstResourceId = 1;

/** Last resource id which may be handed out to a copied icon. */
constexpr std::uint32_t kLastResourceId = 0xFFFF;

/**
 * @brief Resource name of the icon group which is added to the loader payload.
 *
 * The resource directory orders the named groups before the numeric ids and
 * sorts the named groups alphabetically, and the shell shows the first group
 * of that order for a file. The leading exclamation mark sorts before every
 * name a resource script can produce, so the added group wins over the icon
 * groups of the loader without touching them.
 */
const wchar_t* const kApplicationIconGroup = L"!AppBoxIcon";

/** Number of attempts of the resource update of one image. */
constexpr int kUpdateAttempts = 4;

/**
 * @brief Wait between two attempts of the resource update in milliseconds.
 *
 * A filter driver which scans the freshly written image holds the file for a
 * moment, so a denied update is repeated after a short wait.
 */
constexpr DWORD kUpdateRetryDelayMs = 250;

#pragma pack(push, 1)

/** Header of a RT_GROUP_ICON resource. */
struct GroupIconDirectory
{
    WORD reserved; /* Always zero. */
    WORD type;     /* Always one for icons. */
    WORD count;    /* Number of directory entries below. */
};

/** One icon of a RT_GROUP_ICON resource. */
struct GroupIconDirectoryEntry
{
    BYTE  width;        /* Icon width in pixels, zero for 256. */
    BYTE  height;       /* Icon height in pixels, zero for 256. */
    BYTE  colour_count; /* Number of colours, zero for true colour. */
    BYTE  reserved;     /* Always zero. */
    WORD  planes;       /* Colour planes of the image. */
    WORD  bit_count;    /* Bits per pixel of the image. */
    DWORD bytes_in_res; /* Size of the RT_ICON image below. */
    WORD  id;           /* Resource id of the RT_ICON image. */
};

#pragma pack(pop)

static_assert(sizeof(GroupIconDirectory) == kGroupIconHeaderSize,
              "the group icon header has to match the resource layout");
static_assert(sizeof(GroupIconDirectoryEntry) == kGroupIconEntrySize,
              "a group icon entry has to match the resource layout");

/**
 * @brief RAII wrapper of a module handle returned by LoadLibraryExW.
 *
 * The handle of an image which was loaded as a data file has to be released
 * before the file can be opened for writing by the resource update API.
 */
class LoadedImage
{
public:
    /**
     * @brief Take ownership of a module handle.
     * @param[in] module Module handle, may be nullptr.
     */
    explicit LoadedImage(HMODULE module)
        : module_(module)
    {
    }

    ~LoadedImage()
    {
        if (module_ != nullptr)
        {
            FreeLibrary(module_);
        }
    }

    LoadedImage(const LoadedImage&) = delete;
    LoadedImage& operator=(const LoadedImage&) = delete;

    /**
     * @brief Get the module handle.
     * @return The handle, nullptr when the image was not loaded.
     */
    HMODULE Get() const
    {
        return module_;
    }

    /**
     * @brief Whether the image was loaded.
     * @return true when the handle is valid.
     */
    explicit operator bool() const
    {
        return module_ != nullptr;
    }

private:
    HMODULE module_;
};

/**
 * @brief RAII wrapper of a resource update session.
 *
 * The session is discarded unless Commit() was called, so every error path
 * leaves the file unchanged.
 */
class UpdateSession
{
public:
    /**
     * @brief Take ownership of a resource update handle.
     * @param[in] handle Handle returned by BeginUpdateResourceW.
     */
    explicit UpdateSession(HANDLE handle)
        : handle_(handle)
    {
    }

    ~UpdateSession()
    {
        if (handle_ != nullptr)
        {
            /* Discard the pending changes of an uncommitted session. */
            EndUpdateResourceW(handle_, TRUE);
        }
    }

    UpdateSession(const UpdateSession&) = delete;
    UpdateSession& operator=(const UpdateSession&) = delete;

    /**
     * @brief Get the update handle.
     * @return The handle.
     */
    HANDLE Get() const
    {
        return handle_;
    }

    /**
     * @brief Write the pending resources to the file.
     * @return true on success.
     */
    bool Commit()
    {
        if (handle_ == nullptr)
        {
            return false;
        }

        const HANDLE handle = handle_;
        handle_ = nullptr;
        return EndUpdateResourceW(handle, FALSE) != FALSE;
    }

private:
    HANDLE handle_;
};

/**
 * @brief RAII wrapper of a temporary file.
 */
class TemporaryFile
{
public:
    /**
     * @brief Remember the path of the file to remove.
     * @param[in] path Path of the temporary file.
     */
    explicit TemporaryFile(std::filesystem::path path)
        : path_(std::move(path))
    {
    }

    ~TemporaryFile()
    {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;

    /**
     * @brief Get the path of the temporary file.
     * @return The path.
     */
    const std::filesystem::path& Path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_;
};

/**
 * @brief Description of the icon group which the shell shows for a file.
 */
struct GroupIconChoice
{
    bool         found = false;   /* Whether the image has an icon group. */
    bool         numeric = false; /* Whether the group is addressed by id. */
    WORD         id = 0;          /* Resource id of a numeric group. */
    std::wstring name;            /* Resource name of a named group. */
};

/**
 * @brief Callback collecting the numeric resource ids of one type.
 *
 * @param[in] module Module of the enumeration, unused.
 * @param[in] type Resource type of the enumeration, unused.
 * @param[in] name Resource name or id.
 * @param[in] param The `std::vector<WORD>` collecting the ids.
 * @return TRUE to continue the enumeration.
 */
BOOL CALLBACK CollectNumericId(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    static_cast<void>(module);
    static_cast<void>(type);

    if (IS_INTRESOURCE(name))
    {
        auto* ids = reinterpret_cast<std::vector<WORD>*>(param);
        ids->push_back(static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name)));
    }
    return TRUE;
}

/**
 * @brief Callback capturing the first icon group of the resource directory.
 *
 * The resource directory is enumerated in the order the shell uses, so the
 * first entry is the group which Explorer shows for the file.
 *
 * @param[in] module Module of the enumeration, unused.
 * @param[in] type Resource type of the enumeration, unused.
 * @param[in] name Resource name or id.
 * @param[in] param The GroupIconChoice receiving the group.
 * @return FALSE to stop the enumeration after the first entry.
 */
BOOL CALLBACK CaptureFirstGroupIcon(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    static_cast<void>(module);
    static_cast<void>(type);

    auto* choice = reinterpret_cast<GroupIconChoice*>(param);
    if (IS_INTRESOURCE(name))
    {
        choice->numeric = true;
        choice->id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
    }
    else
    {
        choice->numeric = false;
        choice->name = name;
    }
    choice->found = true;
    return FALSE;
}

/**
 * @brief Whether a buffer starts with a PE image.
 *
 * @param[in] bytes Buffer to inspect.
 * @param[in] size Buffer size in bytes.
 * @return true when the DOS and the PE signature are present.
 */
bool LooksLikePeImage(const void* bytes, std::size_t size)
{
    if (bytes == nullptr || size < kPeSignatureOffset + sizeof(std::uint32_t))
    {
        return false;
    }

    const auto* data = static_cast<const unsigned char*>(bytes);
    if (data[0] != 'M' || data[1] != 'Z')
    {
        return false;
    }

    std::uint32_t signature_offset = 0;
    std::memcpy(&signature_offset, data + kPeSignatureOffset, sizeof(signature_offset));
    if (signature_offset + 4 > size)
    {
        return false;
    }

    return data[signature_offset] == 'P' && data[signature_offset + 1] == 'E'
           && data[signature_offset + 2] == 0 && data[signature_offset + 3] == 0;
}

/**
 * @brief Map an executable as a data file without running it.
 *
 * LOAD_LIBRARY_AS_IMAGE_RESOURCE lets the resource functions return pointers
 * into the mapping; plain data file loading is used as the fallback when a
 * mapping is refused. Both forms work for 32 bit and 64 bit images and never
 * execute the mapped code.
 *
 * @param[in] path Host path of the image.
 * @param[out] error Error description on failure.
 * @return The module handle, nullptr on failure.
 */
HMODULE LoadAsDataFile(const std::wstring& path, std::string& error)
{
    const DWORD flags[] = {LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE,
                           LOAD_LIBRARY_AS_DATAFILE};
    for (const auto flag : flags)
    {
        if (HMODULE module = LoadLibraryExW(path.c_str(), nullptr, flag); module != nullptr)
        {
            return module;
        }
    }

    error = "failed to open '" + appbox::WideToUTF8(path) + "' as a data file (error "
            + std::to_string(GetLastError()) + ")";
    return nullptr;
}

/**
 * @brief Get the numeric resource ids of one resource type.
 *
 * @param[in] module Module to enumerate.
 * @param[in] type Resource type to enumerate.
 * @return The ids in the order of the resource directory.
 */
std::vector<WORD> NumericResourceIds(HMODULE module, LPCWSTR type)
{
    std::vector<WORD> ids;
    EnumResourceNamesW(module, type, CollectNumericId, reinterpret_cast<LONG_PTR>(&ids));
    return ids;
}

/**
 * @brief Copy the icon group of an image and the images it references.
 *
 * @param[in] module Module of the image, loaded as a data file.
 * @param[out] group Raw RT_GROUP_ICON content of the first icon group.
 * @param[out] images Raw RT_ICON content of every image of the group.
 * @param[out] error Error description on failure.
 * @return true when the icon group was copied.
 */
bool CollectIconGroup(HMODULE module, std::vector<char>& group, std::vector<std::vector<char>>& images,
                      std::string& error)
{
    GroupIconChoice choice;
    EnumResourceNamesW(module, RT_GROUP_ICON, CaptureFirstGroupIcon,
                       reinterpret_cast<LONG_PTR>(&choice));
    if (!choice.found)
    {
        error = "the image does not carry an icon group";
        return false;
    }

    const HRSRC resource = choice.numeric
                               ? FindResourceW(module, MAKEINTRESOURCEW(choice.id), RT_GROUP_ICON)
                               : FindResourceW(module, choice.name.c_str(), RT_GROUP_ICON);
    if (resource == nullptr)
    {
        error = "the icon group cannot be located (error " + std::to_string(GetLastError()) + ")";
        return false;
    }

    const DWORD group_size = SizeofResource(module, resource);
    const HGLOBAL group_handle = LoadResource(module, resource);
    const void* const group_data = group_handle != nullptr ? LockResource(group_handle) : nullptr;
    if (group_data == nullptr || group_size < kGroupIconHeaderSize)
    {
        error = "the icon group of the image is empty";
        return false;
    }

    group.assign(static_cast<const char*>(group_data),
                 static_cast<const char*>(group_data) + group_size);

    GroupIconDirectory directory = {};
    std::memcpy(&directory, group.data(), sizeof(directory));
    if (directory.count == 0
        || group.size() != kGroupIconHeaderSize + static_cast<std::size_t>(directory.count) * kGroupIconEntrySize)
    {
        error = "the icon group of the image is malformed";
        return false;
    }

    images.clear();
    images.reserve(directory.count);
    for (std::uint32_t index = 0; index < directory.count; ++index)
    {
        GroupIconDirectoryEntry entry = {};
        std::memcpy(&entry, group.data() + kGroupIconHeaderSize + index * kGroupIconEntrySize,
                    sizeof(entry));

        const HRSRC image_resource = FindResourceW(module, MAKEINTRESOURCEW(entry.id), RT_ICON);
        if (image_resource == nullptr)
        {
            error = "the icon group references the missing image "
                    + std::to_string(entry.id);
            return false;
        }

        const DWORD image_size = SizeofResource(module, image_resource);
        const HGLOBAL image_handle = LoadResource(module, image_resource);
        const void* const image_data = image_handle != nullptr ? LockResource(image_handle) : nullptr;
        if (image_data == nullptr || image_size != entry.bytes_in_res)
        {
            error = "the image " + std::to_string(entry.id) + " of the icon group is unusable";
            return false;
        }

        images.emplace_back(static_cast<const char*>(image_data),
                            static_cast<const char*>(image_data) + image_size);
    }

    return true;
}

/**
 * @brief Compare two resource names the way the resource API does.
 *
 * The resource update API stores the names of the resources in upper case and
 * the lookup functions compare them case insensitively, so the comparison
 * follows that rule instead of comparing the strings literally.
 *
 * @param[in] left First resource name.
 * @param[in] right Second resource name.
 * @return true when both names address the same resource.
 */
bool SameResourceName(const std::wstring& left, const std::wstring& right)
{
    const auto result = CompareStringOrdinal(left.c_str(), static_cast<int>(left.size()),
                                             right.c_str(), static_cast<int>(right.size()), TRUE);
    return result == CSTR_EQUAL;
}

/**
 * @brief Whether an image shows one icon group as its first group.
 *
 * The shell shows the first group of the resource directory order for a file,
 * so the group which was added has to be that first entry for the icon of the
 * application to be used.
 *
 * @param[in] path Host path of the image.
 * @param[in] name Resource name of the expected group.
 * @return true when the first group of the image carries the name.
 */
bool IsFirstIconGroup(const std::filesystem::path& path, const std::wstring& name)
{
    std::string open_error;
    LoadedImage  image(LoadAsDataFile(path.wstring(), open_error));
    if (!image)
    {
        return false;
    }

    GroupIconChoice choice;
    EnumResourceNamesW(image.Get(), RT_GROUP_ICON, CaptureFirstGroupIcon,
                       reinterpret_cast<LONG_PTR>(&choice));
    return choice.found && !choice.numeric && SameResourceName(choice.name, name);
}

/**
 * @brief Find the first resource id above every id in use.
 *
 * @param[in] used Ids which the image already uses.
 * @param[in] count Number of consecutive ids which are needed.
 * @param[out] base The first id of the free range.
 * @return true when the range fits into the resource id space.
 */
bool FindResourceIdRange(const std::vector<WORD>& used, std::size_t count, WORD& base)
{
    /* windows.h defines the min and max macros, so the comparison is spelled out. */
    std::uint32_t first = kFirstResourceId;
    for (const auto id : used)
    {
        const auto next = static_cast<std::uint32_t>(id) + 1;
        if (next > first)
        {
            first = next;
        }
    }

    if (count == 0 || first + static_cast<std::uint32_t>(count) - 1 > kLastResourceId)
    {
        return false;
    }

    base = static_cast<WORD>(first);
    return true;
}

/**
 * @brief Point every entry of a copied icon group at its new image.
 *
 * Only the resource id of the entries is rewritten; the size and the format of
 * the images stay exactly as the application declared them.
 *
 * @param[in,out] group Raw RT_GROUP_ICON content.
 * @param[in] base First resource id of the copied images.
 */
void RemapIconIds(std::vector<char>& group, WORD base)
{
    GroupIconDirectory directory = {};
    std::memcpy(&directory, group.data(), sizeof(directory));

    for (std::size_t index = 0; index < directory.count; ++index)
    {
        const std::size_t offset = kGroupIconHeaderSize + index * kGroupIconEntrySize;
        GroupIconDirectoryEntry entry = {};
        std::memcpy(&entry, group.data() + offset, sizeof(entry));
        entry.id = static_cast<WORD>(base + index);
        std::memcpy(group.data() + offset, &entry, sizeof(entry));
    }
}

/**
 * @brief Add the icon resources of an application to an image.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] group Raw RT_GROUP_ICON content to add.
 * @param[in] images Raw RT_ICON content of the group.
 * @param[in] image_base Resource id of the first image.
 * @param[out] error Error description on failure.
 * @param[out] code Error code of the resource API on failure.
 * @return true when the icon group was added.
 */
bool AddIconResources(const std::filesystem::path& path, const std::vector<char>& group,
                      const std::vector<std::vector<char>>& images, WORD image_base,
                      std::string& error, DWORD& code)
{
    code = 0;

    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr)
    {
        code = GetLastError();
        error = "failed to open '" + appbox::WideToUTF8(path.wstring())
                + "' for the icon update (error " + std::to_string(code) + ")";
        return false;
    }

    UpdateSession session(update);
    for (std::size_t index = 0; index < images.size(); ++index)
    {
        const auto id = static_cast<WORD>(image_base + index);
        const auto& image = images[index];
        if (!UpdateResourceW(session.Get(), RT_ICON, MAKEINTRESOURCEW(id), LANG_NEUTRAL,
                             const_cast<char*>(image.data()), static_cast<DWORD>(image.size())))
        {
            code = GetLastError();
            error = "failed to add the icon image " + std::to_string(id) + " (error "
                    + std::to_string(code) + ")";
            return false;
        }
    }

    if (!UpdateResourceW(session.Get(), RT_GROUP_ICON, kApplicationIconGroup, LANG_NEUTRAL,
                         const_cast<char*>(group.data()), static_cast<DWORD>(group.size())))
    {
        code = GetLastError();
        error = "failed to add the icon group (error " + std::to_string(code) + ")";
        return false;
    }

    if (!session.Commit())
    {
        code = GetLastError();
        error = "failed to write the icon of '" + appbox::WideToUTF8(path.wstring()) + "' (error "
                + std::to_string(code) + ")";
        return false;
    }

    return true;
}

/**
 * @brief Write the copied icon group into an image.
 *
 * The group is added below the resource name kApplicationIconGroup, which the
 * resource directory orders before the icon groups of the loader, so the shell
 * shows the icon of the application for the file. The images are added below
 * the ids which the image does not use yet, so the icon groups of the loader
 * keep their own images.
 *
 * The resource update of an image which was written moments ago can be denied
 * while a file system filter - the on access scanner of an antivirus product
 * for example - still holds the file, so a denied update is repeated a few
 * times before the icon is given up.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] group Raw RT_GROUP_ICON content to add.
 * @param[in] images Raw RT_ICON content of the group.
 * @param[out] error Error description on failure.
 * @return true when the icon group was added.
 */
bool WriteIconGroup(const std::filesystem::path& path, const std::vector<char>& group,
                    const std::vector<std::vector<char>>& images, std::string& error)
{
    std::vector<WORD> used_images;
    {
        LoadedImage image(LoadAsDataFile(path.wstring(), error));
        if (!image)
        {
            return false;
        }

        used_images = NumericResourceIds(image.Get(), RT_ICON);
    } /* The mapping has to be released before the file can be written. */

    WORD image_base = 0;
    if (!FindResourceIdRange(used_images, images.size(), image_base))
    {
        error = "the image does not have a free resource id for the icon";
        return false;
    }

    std::vector<char> remapped = group;
    RemapIconIds(remapped, image_base);

    for (int attempt = 0; attempt < kUpdateAttempts; ++attempt)
    {
        if (attempt > 0)
        {
            Sleep(kUpdateRetryDelayMs);
        }

        DWORD code = 0;
        if (AddIconResources(path, remapped, images, image_base, error, code))
        {
            /*
             * The icon is only used when the group really is the first one of
             * the resource directory; otherwise the file keeps the icon of the
             * loader.
             */
            if (!IsFirstIconGroup(path, kApplicationIconGroup))
            {
                error = "the icon group of the application does not precede the icon groups of '"
                        + appbox::WideToUTF8(path.wstring()) + "'";
                return false;
            }

            return true;
        }

        if (code != ERROR_ACCESS_DENIED && code != ERROR_SHARING_VIOLATION)
        {
            break;
        }
    }

    return false;
}

/**
 * @brief Write a buffer to a file.
 *
 * @param[in] path Destination path.
 * @param[in] data Buffer to write.
 * @param[in] size Buffer size in bytes.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteFileBytes(const std::filesystem::path& path, const void* data, std::size_t size,
                    std::string& error)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "failed to create '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
    file.close();
    if (!file)
    {
        error = "failed to write '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    return true;
}

/**
 * @brief Read a whole file into a buffer.
 *
 * @param[in] path Source path.
 * @param[out] bytes The file content.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool ReadFileBytes(const std::filesystem::path& path, std::vector<char>& bytes, std::string& error)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
    {
        error = "failed to open '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    const auto size = file.tellg();
    if (size < 0)
    {
        error = "failed to measure '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    if (!file)
    {
        error = "failed to read '" + appbox::WideToUTF8(path.wstring()) + "'";
        return false;
    }

    return true;
}

/**
 * @brief Build the path of the temporary payload image.
 *
 * @return A unique path below the temporary directory of the process.
 */
std::filesystem::path TemporaryPayloadPath()
{
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto name = L"AppBox-Icon-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                      + std::to_wstring(ticks) + L".exe";
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

namespace appbox
{

std::vector<char> ApplyApplicationIcon(const void* loader_bytes, std::size_t loader_size,
                                       const std::wstring& application_path, std::string& warning)
{
    warning.clear();

    if (loader_bytes == nullptr || loader_size == 0)
    {
        warning = "the loader payload is empty";
        return {};
    }
    if (!LooksLikePeImage(loader_bytes, loader_size))
    {
        warning = "the loader payload is not a PE image";
        return {};
    }
    if (application_path.empty())
    {
        warning = "the path of the main program is empty";
        return {};
    }

    const auto application = WideToUTF8(application_path);
    std::string error;
    try
    {
        std::vector<char>                    group;
        std::vector<std::vector<char>>       images;
        {
            LoadedImage module(LoadAsDataFile(application_path, error));
            if (!module)
            {
                warning = error;
                return {};
            }

            if (!CollectIconGroup(module.Get(), group, images, error))
            {
                warning = "'" + application + "' has no usable icon: " + error;
                return {};
            }
        }

        const auto temporary_path = TemporaryPayloadPath();
        const TemporaryFile payload(temporary_path);
        if (!WriteFileBytes(temporary_path, loader_bytes, loader_size, error))
        {
            warning = error;
            return {};
        }

        if (!WriteIconGroup(temporary_path, group, images, error))
        {
            warning = error;
            return {};
        }

        std::vector<char> patched;
        if (!ReadFileBytes(temporary_path, patched, error))
        {
            warning = error;
            return {};
        }

        return patched;
    }
    catch (const std::exception& e)
    {
        warning = std::string("failed to apply the application icon: ") + e.what();
        return {};
    }
}

} // namespace appbox

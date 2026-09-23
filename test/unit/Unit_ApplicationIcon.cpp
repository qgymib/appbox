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
#include <gtest/gtest.h>
#include "src/core/ApplicationIcon.hpp"
#include "src/core/PackService.hpp"
#include "src/core/ZipReader.hpp"
#include "unit/LoaderPath.hpp"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace
{

/** Size of the header of a RT_GROUP_ICON resource. */
constexpr std::size_t kGroupIconHeaderSize = 6;

/** Size of one entry of a RT_GROUP_ICON resource. */
constexpr std::size_t kGroupIconEntrySize = 14;

/**
 * @brief Name of the icon group which the loader carries for its own window.
 *
 * The real loader embeds it with resource.rc; the test fixtures add a group
 * with the same name to stand in for it.
 */
constexpr const wchar_t* kLoaderIconGroup = L"IDI_ICON1";

/**
 * @brief Resource name of the icon group which the packer adds.
 *
 * The name has to sort before every group of the loader, because the shell
 * shows the first group of the resource directory order for a file.
 */
constexpr const wchar_t* kApplicationIconGroup = L"!AppBoxIcon";

#pragma pack(push, 1)

/** One entry of a RT_GROUP_ICON resource. */
struct GroupIconEntry
{
    BYTE  width;
    BYTE  height;
    BYTE  colour_count;
    BYTE  reserved;
    WORD  planes;
    WORD  bit_count;
    DWORD bytes_in_res;
    WORD  id;
};

#pragma pack(pop)

static_assert(sizeof(GroupIconEntry) == kGroupIconEntrySize,
              "a group icon entry has to match the resource layout");

/**
 * @brief RAII helper creating a unique folder below the temporary directory.
 */
class TempDir
{
public:
    TempDir()
    {
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
                / (L"appbox-application-icon-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                   + std::to_wstring(ticks) + L"-" + std::to_wstring(++counter_));
        std::filesystem::create_directories(path_);
    }

    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    /**
     * @brief Get the folder path.
     * @return The folder path.
     */
    const std::filesystem::path& Get() const
    {
        return path_;
    }

private:
    static unsigned counter_;
    std::filesystem::path path_;
};

unsigned TempDir::counter_ = 0;

/**
 * @brief One entry of a resource directory.
 */
struct ResourceEntry
{
    bool         numeric = false; /* Whether the entry is addressed by id. */
    WORD         id = 0;          /* Resource id of a numeric entry. */
    std::wstring name;            /* Resource name of a named entry. */
};

/**
 * @brief Callback collecting the entries of a resource directory.
 *
 * @param[in] module Module of the enumeration, unused.
 * @param[in] type Resource type of the enumeration, unused.
 * @param[in] name Resource name or id.
 * @param[in] param The `std::vector<ResourceEntry>` collecting the entries.
 * @return TRUE to continue the enumeration.
 */
BOOL CALLBACK CaptureEntry(HMODULE module, LPCWSTR type, LPWSTR name, LONG_PTR param)
{
    static_cast<void>(module);
    static_cast<void>(type);

    auto* entries = reinterpret_cast<std::vector<ResourceEntry>*>(param);
    ResourceEntry entry;
    if (IS_INTRESOURCE(name))
    {
        entry.numeric = true;
        entry.id = static_cast<WORD>(reinterpret_cast<ULONG_PTR>(name));
    }
    else
    {
        entry.name = name;
    }

    entries->push_back(std::move(entry));
    return TRUE;
}

/**
 * @brief Get the path of the running test executable.
 * @return The path of the test executable.
 */
std::wstring SelfPath()
{
    std::vector<wchar_t> buffer(32768, L'\0');
    const auto length = GetModuleFileNameW(nullptr, buffer.data(),
                                           static_cast<DWORD>(buffer.size()));
    return std::wstring(buffer.data(), length);
}

/**
 * @brief Copy a file.
 * @param[in] source Source path.
 * @param[in] destination Destination path.
 * @return true on success.
 */
bool CopyFileTo(const std::wstring& source, const std::wstring& destination)
{
    return CopyFileW(source.c_str(), destination.c_str(), FALSE) != FALSE;
}

/**
 * @brief Read a whole file.
 * @param[in] path File path.
 * @return The file content, empty on failure.
 */
std::vector<char> ReadAllBytes(const std::wstring& path)
{
    std::ifstream file(std::filesystem::path(path), std::ios::binary | std::ios::ate);
    if (!file)
    {
        return {};
    }

    const auto size = file.tellg();
    if (size < 0)
    {
        return {};
    }

    std::vector<char> bytes(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (!bytes.empty())
    {
        file.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    return file ? bytes : std::vector<char>();
}

/**
 * @brief Write a whole file.
 * @param[in] path File path.
 * @param[in] bytes File content.
 * @return true on success.
 */
bool WriteAllBytes(const std::wstring& path, const std::vector<char>& bytes)
{
    std::ofstream file(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
    if (!file)
    {
        return false;
    }

    if (!bytes.empty())
    {
        file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    file.close();
    return file ? true : false;
}

/**
 * @brief Build the content of one RT_ICON resource.
 *
 * The image is a 32 bit DIB with a solid colour and an empty AND mask, which
 * is the layout the resource compiler produces for the frames of an icon file.
 *
 * @param[in] size Edge length of the icon in pixels.
 * @param[in] colour Colour of the icon as 0x00BBGGRR.
 * @return The image content.
 */
std::vector<char> MakeIconImage(std::uint32_t size, std::uint32_t colour)
{
    BITMAPINFOHEADER header = {};
    header.biSize = sizeof(header);
    header.biWidth = static_cast<LONG>(size);
    header.biHeight = static_cast<LONG>(size * 2); /* The XOR mask above the AND mask. */
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;

    std::vector<char> image(sizeof(header));
    std::memcpy(image.data(), &header, sizeof(header));

    const char channels[4] = {static_cast<char>(colour), static_cast<char>(colour >> 8),
                              static_cast<char>(colour >> 16), static_cast<char>(0xFF)};
    for (std::uint32_t pixel = 0; pixel < size * size; ++pixel)
    {
        image.insert(image.end(), channels, channels + 4);
    }

    /* The AND mask of a 32 bit icon: one bit per pixel, rows padded to 4 bytes. */
    const std::size_t row = ((static_cast<std::size_t>(size) + 31) / 32) * 4;
    image.insert(image.end(), row * size, '\0');
    return image;
}

/**
 * @brief Build the content of a RT_GROUP_ICON resource.
 * @param[in] first_id Resource id of the first image.
 * @param[in] images Images of the group, in the order of the group.
 * @return The group content.
 */
std::vector<char> MakeIconGroup(WORD first_id, const std::vector<std::vector<char>>& images)
{
    std::vector<char> group(kGroupIconHeaderSize + images.size() * kGroupIconEntrySize, '\0');

    const WORD header[3] = {0, 1, static_cast<WORD>(images.size())};
    std::memcpy(group.data(), header, sizeof(header));

    for (std::size_t index = 0; index < images.size(); ++index)
    {
        BITMAPINFOHEADER info = {};
        std::memcpy(&info, images[index].data(), sizeof(info));

        const auto height = static_cast<std::uint32_t>(info.biHeight / 2);
        GroupIconEntry entry = {};
        entry.width = static_cast<BYTE>(info.biWidth == 256 ? 0 : info.biWidth);
        entry.height = static_cast<BYTE>(height == 256 ? 0 : height);
        entry.planes = info.biPlanes;
        entry.bit_count = info.biBitCount;
        entry.bytes_in_res = static_cast<DWORD>(images[index].size());
        entry.id = static_cast<WORD>(first_id + index);
        std::memcpy(group.data() + kGroupIconHeaderSize + index * kGroupIconEntrySize, &entry,
                    sizeof(entry));
    }

    return group;
}

/**
 * @brief Run one attempt of the resource update of an image.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] group Resource id or name of the icon group.
 * @param[in] first_id Resource id of the first image.
 * @param[in] images Images of the group.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool AddIconGroupOnce(const std::wstring& path, LPCWSTR group, WORD first_id,
                      const std::vector<std::vector<char>>& images, std::string& error)
{
    const HANDLE update = BeginUpdateResourceW(path.c_str(), FALSE);
    if (update == nullptr)
    {
        error = "BeginUpdateResourceW failed with " + std::to_string(GetLastError());
        return false;
    }

    for (std::size_t index = 0; index < images.size(); ++index)
    {
        const auto id = static_cast<WORD>(first_id + index);
        if (!UpdateResourceW(update, RT_ICON, MAKEINTRESOURCEW(id), LANG_NEUTRAL,
                             const_cast<char*>(images[index].data()),
                             static_cast<DWORD>(images[index].size())))
        {
            error = "UpdateResourceW(RT_ICON) failed with " + std::to_string(GetLastError());
            EndUpdateResourceW(update, TRUE);
            return false;
        }
    }

    const auto group_bytes = MakeIconGroup(first_id, images);
    if (!UpdateResourceW(update, RT_GROUP_ICON, group, LANG_NEUTRAL,
                         const_cast<char*>(group_bytes.data()),
                         static_cast<DWORD>(group_bytes.size())))
    {
        error = "UpdateResourceW(RT_GROUP_ICON) failed with " + std::to_string(GetLastError());
        EndUpdateResourceW(update, TRUE);
        return false;
    }

    if (!EndUpdateResourceW(update, FALSE))
    {
        error = "EndUpdateResourceW failed with " + std::to_string(GetLastError());
        return false;
    }

    return true;
}

/**
 * @brief Add an icon group to an image with the Windows resource API.
 *
 * The helper builds the fixtures of the tests: the application which donates
 * the icon and the loader which already carries its own icon group.
 *
 * The update is attempted several times, because the image is a freshly copied
 * executable and a virus scanner which inspects it can hold the file for a
 * moment, which makes `EndUpdateResourceW` fail with `ERROR_ACCESS_DENIED`.
 *
 * @param[in] path Host path of the image to patch.
 * @param[in] group Resource id or name of the icon group.
 * @param[in] first_id Resource id of the first image.
 * @param[in] images Images of the group.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool AddIconGroup(const std::wstring& path, LPCWSTR group, WORD first_id,
                  const std::vector<std::vector<char>>& images, std::string& error)
{
    /** Number of attempts of the resource update. */
    constexpr int kAttempts = 5;

    /** Delay between two attempts. */
    constexpr auto kRetryDelay = std::chrono::milliseconds(100);

    for (int attempt = 0; attempt < kAttempts; ++attempt)
    {
        if (attempt != 0)
        {
            std::this_thread::sleep_for(kRetryDelay);
        }

        if (AddIconGroupOnce(path, group, first_id, images, error))
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Get the entries of one resource type in the order of the directory.
 * @param[in] path Host path of the image.
 * @param[in] type Resource type to enumerate.
 * @return The entries, empty when the image cannot be read.
 */
std::vector<ResourceEntry> ResourceEntries(const std::wstring& path, LPCWSTR type)
{
    std::vector<ResourceEntry> entries;
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr)
    {
        return entries;
    }

    EnumResourceNamesW(module, type, CaptureEntry, reinterpret_cast<LONG_PTR>(&entries));
    FreeLibrary(module);
    return entries;
}

/**
 * @brief Get the resource name of an entry for the lookup functions.
 * @param[in] entry Resource directory entry.
 * @return The name or the id of the entry.
 */
LPCWSTR ResourceName(const ResourceEntry& entry)
{
    return entry.numeric ? MAKEINTRESOURCEW(entry.id) : entry.name.c_str();
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
 * @brief Load one resource of an image as raw bytes.
 * @param[in] path Host path of the image.
 * @param[in] type Resource type to load.
 * @param[in] name Resource id or name to load.
 * @return The resource content, empty when the resource does not exist.
 */
std::vector<char> ReadResource(const std::wstring& path, LPCWSTR type, LPCWSTR name)
{
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr,
                                          LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (module == nullptr)
    {
        return {};
    }

    std::vector<char> bytes;
    const HRSRC resource = FindResourceW(module, name, type);
    if (resource != nullptr)
    {
        const DWORD size = SizeofResource(module, resource);
        const HGLOBAL handle = LoadResource(module, resource);
        const void* const data = handle != nullptr ? LockResource(handle) : nullptr;
        if (data != nullptr)
        {
            bytes.assign(static_cast<const char*>(data), static_cast<const char*>(data) + size);
        }
    }

    FreeLibrary(module);
    return bytes;
}

/**
 * @brief Parse the entries of a RT_GROUP_ICON resource.
 * @param[in] group Raw content of the resource.
 * @return The entries, empty when the content is malformed.
 */
std::vector<GroupIconEntry> ParseIconGroup(const std::vector<char>& group)
{
    std::vector<GroupIconEntry> entries;
    if (group.size() < kGroupIconHeaderSize)
    {
        return entries;
    }

    /* The header holds the reserved word, the type and then the entry count. */
    WORD count = 0;
    std::memcpy(&count, group.data() + 2 * sizeof(WORD), sizeof(count));
    for (WORD index = 0; index < count; ++index)
    {
        GroupIconEntry entry = {};
        std::memcpy(&entry, group.data() + kGroupIconHeaderSize + index * kGroupIconEntrySize,
                    sizeof(entry));
        entries.push_back(entry);
    }

    return entries;
}

/**
 * @brief Render the file icon of an image into 32 bit pixels.
 *
 * ExtractIconExW reads the icon groups of a file in the order of the resource
 * directory, which is the order the shell uses, so the helper renders the icon
 * Explorer shows for the file.
 *
 * @param[in] path Host path of the image.
 * @param[in] large Whether the large (true) or the small icon is rendered.
 * @param[in] size Edge length of the target bitmap in pixels.
 * @return The pixels of the icon, empty when the file has no icon.
 */
std::vector<unsigned char> RenderFileIcon(const std::wstring& path, bool large, int size)
{
    HICON icon = nullptr;
    const auto count = large ? ExtractIconExW(path.c_str(), 0, &icon, nullptr, 1)
                             : ExtractIconExW(path.c_str(), 0, nullptr, &icon, 1);
    if (count == 0 || icon == nullptr)
    {
        return {};
    }

    const HDC screen = GetDC(nullptr);
    const HDC memory = CreateCompatibleDC(screen);

    BITMAPINFO info = {};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = size;
    info.bmiHeader.biHeight = -size; /* Top down, so the rows compare directly. */
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;

    void*          bits = nullptr;
    const HBITMAP  bitmap = CreateDIBSection(memory, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    const HGDIOBJ  previous = SelectObject(memory, bitmap);
    DrawIconEx(memory, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);
    GdiFlush();

    std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * static_cast<std::size_t>(size)
                                      * 4);
    std::memcpy(pixels.data(), bits, pixels.size());

    SelectObject(memory, previous);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    DestroyIcon(icon);
    return pixels;
}

} // namespace

TEST(ApplicationIcon, KeepsThePayloadWithoutAPeImage)
{
    std::string warning;
    const auto  patched = appbox::ApplyApplicationIcon("NOT-A-PE-IMAGE", 14, SelfPath(), warning);

    EXPECT_TRUE(patched.empty());
    EXPECT_FALSE(warning.empty());
}

TEST(ApplicationIcon, KeepsThePayloadWhenTheApplicationHasNoIcon)
{
    TempDir temp;
    const auto loader = temp.Get() / L"loader.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), loader.wstring()));

    /* The test executable is built without resources, so it carries no icon. */
    ASSERT_TRUE(ResourceEntries(SelfPath(), RT_GROUP_ICON).empty());

    const auto payload = ReadAllBytes(loader.wstring());
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  patched =
        appbox::ApplyApplicationIcon(payload.data(), payload.size(), SelfPath(), warning);

    EXPECT_TRUE(patched.empty());
    EXPECT_FALSE(warning.empty());
}

TEST(ApplicationIcon, KeepsThePayloadWhenTheApplicationCannotBeRead)
{
    TempDir temp;
    const auto loader = temp.Get() / L"loader.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), loader.wstring()));

    const auto payload = ReadAllBytes(loader.wstring());
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  patched = appbox::ApplyApplicationIcon(payload.data(), payload.size(),
                                                       (temp.Get() / L"missing.exe").wstring(), warning);

    EXPECT_TRUE(patched.empty());
    EXPECT_FALSE(warning.empty());
}

TEST(ApplicationIcon, AppendsTheIconGroupOfTheApplication)
{
    TempDir temp;

    const auto application = temp.Get() / L"app.exe";
    const auto loader = temp.Get() / L"loader.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), application.wstring()));
    ASSERT_TRUE(CopyFileTo(SelfPath(), loader.wstring()));

    std::string error;
    const auto  application_large = MakeIconImage(32, 0x0000FF); /* Red. */
    const auto  application_small = MakeIconImage(16, 0x0000FF);
    ASSERT_TRUE(AddIconGroup(application.wstring(), MAKEINTRESOURCEW(1), 1,
                             {application_large, application_small}, error))
        << error;

    /* The loader stands in with the icon group which resource.rc embeds. */
    const auto loader_icon = MakeIconImage(32, 0x00FF00); /* Green. */
    ASSERT_TRUE(AddIconGroup(loader.wstring(), kLoaderIconGroup, 1, {loader_icon}, error)) << error;

    const auto payload = ReadAllBytes(loader.wstring());
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  patched =
        appbox::ApplyApplicationIcon(payload.data(), payload.size(), application.wstring(), warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(patched.empty());

    const auto patched_path = temp.Get() / L"patched.exe";
    ASSERT_TRUE(WriteAllBytes(patched_path.wstring(), patched));

    /* The added group is the first group of the resource directory. */
    const auto groups = ResourceEntries(patched_path.wstring(), RT_GROUP_ICON);
    ASSERT_EQ(groups.size(), static_cast<std::size_t>(2));
    EXPECT_FALSE(groups.front().numeric);
    EXPECT_TRUE(SameResourceName(groups.front().name, kApplicationIconGroup));

    /* The images of the application are added below free resource ids. */
    EXPECT_EQ(ReadResource(patched_path.wstring(), RT_ICON, MAKEINTRESOURCEW(2)), application_large);
    EXPECT_EQ(ReadResource(patched_path.wstring(), RT_ICON, MAKEINTRESOURCEW(3)), application_small);

    /* The icon group of the loader and its image survive unchanged. */
    EXPECT_EQ(ReadResource(patched_path.wstring(), RT_GROUP_ICON, kLoaderIconGroup),
              ReadResource(loader.wstring(), RT_GROUP_ICON, kLoaderIconGroup));
    EXPECT_EQ(ReadResource(patched_path.wstring(), RT_ICON, MAKEINTRESOURCEW(1)), loader_icon);

    /* The added group points at the images of the application. */
    const auto group = ReadResource(patched_path.wstring(), RT_GROUP_ICON, kApplicationIconGroup);
    const auto entries = ParseIconGroup(group);
    ASSERT_EQ(entries.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(entries[0].id, 2);
    EXPECT_EQ(entries[0].width, 32);
    EXPECT_EQ(entries[0].height, 32);
    EXPECT_EQ(entries[0].bytes_in_res, static_cast<DWORD>(application_large.size()));
    EXPECT_EQ(entries[1].id, 3);
    EXPECT_EQ(entries[1].width, 16);
    EXPECT_EQ(entries[1].height, 16);
    EXPECT_EQ(entries[1].bytes_in_res, static_cast<DWORD>(application_small.size()));

    /* The file icon of the patched program is the icon of the application. */
    EXPECT_NE(RenderFileIcon(loader.wstring(), true, 32),
              RenderFileIcon(patched_path.wstring(), true, 32));
    EXPECT_EQ(RenderFileIcon(application.wstring(), true, 32),
              RenderFileIcon(patched_path.wstring(), true, 32));
    EXPECT_EQ(RenderFileIcon(application.wstring(), false, 16),
              RenderFileIcon(patched_path.wstring(), false, 16));
}

TEST(ApplicationIcon, PackWritesTheIconOfTheMainProgram)
{
    TempDir temp;

    const auto application = temp.Get() / L"MyApp";
    std::filesystem::create_directories(application);
    const auto program = application / L"app.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), program.wstring()));

    std::string error;
    const auto  application_icon = MakeIconImage(32, 0x0000FF); /* Red. */
    ASSERT_TRUE(AddIconGroup(program.wstring(), MAKEINTRESOURCEW(1), 1, {application_icon}, error))
        << error;

    appbox::PackModel model;
    ASSERT_TRUE(model.ImportFolder("program_files", application.wstring(), error)) << error;
    ASSERT_TRUE(model.SetMainProgram("program_files", L"MyApp", L"app.exe", error)) << error;

    /* The test executable stands in for the loader payload. */
    const auto payload = ReadAllBytes(SelfPath());
    ASSERT_FALSE(payload.empty());

    const auto zip_path = temp.Get() / L"out.zip";
    const appbox::RegistryModel registry;
    ASSERT_EQ(appbox::Pack(model, registry, payload.data(), payload.size(), zip_path.wstring(), nullptr), "");

    const auto extracted = temp.Get() / L"extracted";
    ASSERT_EQ(appbox::ExtractArchive(zip_path.wstring(), extracted.wstring()), "");

    /* The extracted program carries the icon of the packaged application. */
    const auto entry = extracted / L"app.exe";
    ASSERT_TRUE(std::filesystem::exists(entry));
    EXPECT_NE(ReadAllBytes(entry.wstring()), payload);
    EXPECT_EQ(RenderFileIcon(entry.wstring(), true, 32), RenderFileIcon(program.wstring(), true, 32));
}

TEST(ApplicationIcon, AppliesTheIconToTheRealLoaderPayload)
{
    const auto loader = appbox::test::LoaderPath();
    if (loader.empty())
    {
        GTEST_SKIP() << "the loader path was not passed with --loader=<path>";
    }

    TempDir temp;
    const auto application = temp.Get() / L"app.exe";
    ASSERT_TRUE(CopyFileTo(SelfPath(), application.wstring()));

    std::string error;
    const auto  application_large = MakeIconImage(48, 0x0000FF); /* Red. */
    const auto  application_small = MakeIconImage(16, 0x0000FF);
    ASSERT_TRUE(AddIconGroup(application.wstring(), MAKEINTRESOURCEW(1), 1,
                             {application_large, application_small}, error))
        << error;

    const auto payload = ReadAllBytes(loader);
    ASSERT_FALSE(payload.empty());

    std::string warning;
    const auto  patched =
        appbox::ApplyApplicationIcon(payload.data(), payload.size(), application.wstring(), warning);
    ASSERT_TRUE(warning.empty()) << warning;
    ASSERT_FALSE(patched.empty());

    const auto patched_path = temp.Get() / L"loader.exe";
    ASSERT_TRUE(WriteAllBytes(patched_path.wstring(), patched));

    /* The added group precedes every icon group of the real loader. */
    const auto patched_groups = ResourceEntries(patched_path.wstring(), RT_GROUP_ICON);
    ASSERT_FALSE(patched_groups.empty());
    EXPECT_FALSE(patched_groups.front().numeric);
    EXPECT_TRUE(SameResourceName(patched_groups.front().name, kApplicationIconGroup));

    /* Every icon resource of the loader survives the update byte for byte. */
    const auto groups = ResourceEntries(loader, RT_GROUP_ICON);
    ASSERT_FALSE(groups.empty());
    for (const auto& group : groups)
    {
        EXPECT_EQ(ReadResource(patched_path.wstring(), RT_GROUP_ICON, ResourceName(group)),
                  ReadResource(loader, RT_GROUP_ICON, ResourceName(group)))
            << group.id;
    }

    const auto images = ResourceEntries(loader, RT_ICON);
    ASSERT_FALSE(images.empty());
    for (const auto& image : images)
    {
        EXPECT_EQ(ReadResource(patched_path.wstring(), RT_ICON, MAKEINTRESOURCEW(image.id)),
                  ReadResource(loader, RT_ICON, MAKEINTRESOURCEW(image.id)))
            << image.id;
    }

    /* The file icon of the patched loader is the icon of the application. */
    EXPECT_NE(RenderFileIcon(loader, true, 48), RenderFileIcon(patched_path.wstring(), true, 48));
    EXPECT_EQ(RenderFileIcon(application.wstring(), true, 48),
              RenderFileIcon(patched_path.wstring(), true, 48));
}

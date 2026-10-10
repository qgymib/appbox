#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <cstddef>
#include <fstream>
#include <system_error>
#include <vector>
#include "WString.hpp"
#include "TestKnownFolder.hpp"
#include "RealFsFolder.hpp"

/** Size of the header of the data of a reparse point. */
static constexpr std::size_t kReparseHeaderSize = sizeof(ULONG) + 2 * sizeof(USHORT);

appbox::test::RealFsFolder::RealFsFolder(const std::wstring& known_folder, const std::wstring& name)
{
    path_ = std::filesystem::path(GetKnownFolderPath(known_folder, false)) / name;

    std::error_code ec;
    std::filesystem::create_directories(path_, ec);
}

appbox::test::RealFsFolder::~RealFsFolder()
{
    /*
     * The junction is removed as the object it is: removing the folder of the
     * case would walk into the target a junction names, which the helper never
     * owns.
     */
    for (const auto& junction : junctions_)
    {
        RemoveDirectoryW(junction.c_str());
    }

    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

bool appbox::test::RealFsFolder::WriteFile(const std::wstring& relative, const std::string& text) const
{
    const auto path = path_ / relative;

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

bool appbox::test::RealFsFolder::FileExists(const std::wstring& relative) const
{
    std::error_code ec;
    return std::filesystem::exists(path_ / relative, ec);
}

bool appbox::test::RealFsFolder::CreateJunction(const std::wstring& relative, const std::wstring& target)
{
    const auto path = path_ / relative;

    std::error_code ec;
    std::filesystem::create_directories(path, ec);

    /*
     * The object is opened without following it, so the reparse point of the
     * junction itself is written.
     */
    HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    const std::wstring substitute = target.compare(0, 4, L"\\??\\") == 0 ? target : L"\\??\\" + target;
    const std::size_t  substituteBytes = (substitute.size() + 1) * sizeof(wchar_t);
    const std::size_t  printBytes = (target.size() + 1) * sizeof(wchar_t);
    const std::size_t  pathBufferOffset = kReparseHeaderSize + 4 * sizeof(USHORT);

    std::vector<BYTE> data(pathBufferOffset + substituteBytes + printBytes, 0);
    auto*             buffer = reinterpret_cast<REPARSE_DATA_BUFFER*>(data.data());
    buffer->ReparseTag = IO_REPARSE_TAG_MOUNT_POINT;
    buffer->ReparseDataLength =
        static_cast<USHORT>((pathBufferOffset - kReparseHeaderSize) + substituteBytes + printBytes);
    buffer->Reserved = 0;

    auto& variant = buffer->ReparseBuffer.MountPointReparseBuffer;
    variant.SubstituteNameOffset = 0;
    variant.SubstituteNameLength = static_cast<USHORT>(substitute.size() * sizeof(wchar_t));
    variant.PrintNameOffset = static_cast<USHORT>(substituteBytes);
    variant.PrintNameLength = static_cast<USHORT>(target.size() * sizeof(wchar_t));

    memcpy(variant.PathBuffer, substitute.c_str(), substituteBytes);
    memcpy(reinterpret_cast<BYTE*>(variant.PathBuffer) + substituteBytes, target.c_str(), printBytes);

    DWORD      returned = 0;
    const BOOL written = DeviceIoControl(handle, FSCTL_SET_REPARSE_POINT, data.data(), static_cast<DWORD>(data.size()),
                                         nullptr, 0, &returned, nullptr);
    CloseHandle(handle);

    if (!written)
    {
        return false;
    }

    junctions_.push_back(path);
    return true;
}

const std::filesystem::path& appbox::test::RealFsFolder::Get() const
{
    return path_;
}

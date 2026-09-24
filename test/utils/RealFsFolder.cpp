#include <fstream>
#include <system_error>
#include "KnownFolder.hpp"
#include "RealFsFolder.hpp"

appbox::test::RealFsFolder::RealFsFolder(const std::wstring& known_folder, const std::wstring& name)
{
    path_ = std::filesystem::path(GetKnownFolderPath(known_folder, false)) / name;

    std::error_code ec;
    std::filesystem::create_directories(path_, ec);
}

appbox::test::RealFsFolder::~RealFsFolder()
{
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

const std::filesystem::path& appbox::test::RealFsFolder::Get() const
{
    return path_;
}

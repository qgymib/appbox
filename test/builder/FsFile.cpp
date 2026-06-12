#include <fstream>
#include "utils/ReadFileFull.hpp"
#include "FsBuilder.hpp"
#include "FsFile.hpp"

static void WriteFile(const std::filesystem::path& path, const appbox::test::FsFile::Bytes& bytes)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

appbox::test::FsFile::FsFile(const std::wstring& name) : FsNode(name)
{
}

appbox::test::FsFile::FsFile(const std::wstring& name, const std::string& text) : FsNode(name)
{
    Bytes bytes(text.begin(), text.end());
    SetContent(bytes);
}

void appbox::test::FsFile::SetContent(const Bytes& bytes)
{
    this->content = bytes;
}

void appbox::test::FsFile::Build(const std::filesystem::path& root) const
{
    auto path = root / GetName();
    auto parent = path.parent_path();

    /* Make sure the parent directory exists. */
    std::filesystem::create_directories(parent);

    /* Write the file. */
    WriteFile(path, this->content);
}

bool appbox::test::FsFile::Verify(const std::filesystem::path& root) const
{
    auto  path = root / GetName();
    Bytes data;

    if (appbox::test::ReadFileFull(path.wstring(), data) != 0)
    {
        return false;
    }
    if (data.size() != this->content.size())
    {
        return false;
    }

    return memcmp(data.data(), this->content.data(), data.size()) == 0;
}

appbox::test::FsNode::Ptr appbox::test::FsFile::Make(const std::wstring& name, const std::string& text)
{
    return std::make_shared<FsFile>(name, text);
}

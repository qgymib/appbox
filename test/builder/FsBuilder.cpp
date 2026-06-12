#include <fstream>
#include <algorithm>
#include <vector>
#include "utils/ReadFileFull.hpp"
#include "FsBuilder.hpp"
#include "WString.hpp"

struct appbox::test::FsNode::Data
{
    Data(const std::wstring& name);
    Data(const std::wstring& name, const Bytes& bytes);

    void SetName(const std::wstring& name);
    bool Build(const std::filesystem::path& root) const;
    bool Verify(const std::filesystem::path& root) const;

    std::vector<std::wstring> name_;
    bool                      isFile_;
    Bytes                     bytes_;
    DataPtrVec                children_;
};

struct appbox::test::FsRoot::Data
{
    std::filesystem::path            root_;
    appbox::test::FsNode::DataPtrVec fs_;
};

static bool WriteFile(const std::filesystem::path& path, const appbox::test::FsNode::Bytes& bytes)
{
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    try
    {
        ofs.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    }
    catch (const std::exception&)
    {
        return false;
    }
    return true;
}

static std::wstring BuildName(const std::vector<std::wstring> name_vec)
{
    std::wstring name;
    auto         name_sz = name_vec.size();
    for (size_t i = 0; i < name_sz; ++i)
    {
        if (i != 0)
        {
            name += L"\\";
        }
        name += name_vec[i];
    }
    return name;
}

static size_t CountFiles(const std::filesystem::path& root)
{
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        (void)entry;
        count++;
    }
    return count;
}

appbox::test::FsNode::Data::Data(const std::wstring& name)
{
    SetName(name);
    isFile_ = false;
}

appbox::test::FsNode::Data::Data(const std::wstring& name, const Bytes& bytes)
{
    SetName(name);
    isFile_ = true;
    bytes_ = bytes;
}

void appbox::test::FsNode::Data::SetName(const std::wstring& name)
{
    auto copy_name = name;
    /* Remove trailing backslashes */
    while (copy_name.back() == L'\\')
    {
        copy_name.pop_back();
    }
    /* Remove leading backslashes */
    while (copy_name.front() == L'\\')
    {
        copy_name.erase(copy_name.begin());
    }

    /* Split by backslashes */
    this->name_ = appbox::Split(copy_name, L"\\");
}

bool appbox::test::FsNode::Data::Build(const std::filesystem::path& root) const
{
    auto current_path = root;
    auto part_sz = name_.size();

    /* Create parent directory */
    for (size_t i = 0; i < part_sz - 1; ++i)
    {
        current_path = current_path / name_[i];
        std::filesystem::create_directories(current_path);
    }
    current_path = current_path / name_.back();

    /* If it is a file, write the data */
    if (isFile_)
    {
        return WriteFile(current_path, bytes_);
    }

    /* If it is a directory, create the directory */
    std::filesystem::create_directories(current_path);

    /* Recursively create child directories */
    for (const auto& child : children_)
    {
        if (!child->Build(current_path))
        {
            return false;
        }
    }

    return true;
}

bool appbox::test::FsNode::Data::Verify(const std::filesystem::path& root) const
{
    auto current_path = root;

    for (size_t i = 0; i < name_.size() - 1; ++i)
    {
        current_path = current_path / name_[i];
        if (!std::filesystem::exists(current_path))
        {
            return false;
        }
    }
    current_path = current_path / name_.back();

    if (isFile_)
    {
        std::vector<uint8_t> data;
        if (appbox::test::ReadFileFull(current_path.wstring(), data) != 0)
        {
            return false;
        }

        if (data.size() != bytes_.size())
        {
            return false;
        }

        return memcmp(data.data(), bytes_.data(), bytes_.size()) == 0;
    }

    if (!std::filesystem::exists(current_path))
    {
        return false;
    }

    if (CountFiles(current_path) != children_.size())
    {
        return false;
    }

    for (const auto& child : children_)
    {
        if (!child->Verify(current_path))
        {
            return false;
        }
    }
    return true;
}

appbox::test::FsNode::FsNode(const std::wstring& name, const FsNode::Nodes& children)
{
    data_ = std::make_shared<Data>(name);

    for (const auto& child : children)
    {
        data_->children_.push_back(child.data_);
    }
}

appbox::test::FsNode::FsNode(const std::wstring& name, const Bytes& bytes)
{
    data_ = std::make_shared<Data>(name, bytes);
}

appbox::test::FsNode::FsNode(const std::wstring& name, const std::string& text)
{
    Bytes bytes(text.begin(), text.end());
    data_ = std::make_shared<Data>(name, bytes);
}

appbox::test::FsFile::FsFile(const std::wstring& name, const std::string& text) : FsNode(name, text)
{
}

appbox::test::FsDir::FsDir(const std::wstring& name, const FsNode::Nodes& children) : FsNode(name, children)
{
}

appbox::test::FsRoot::FsRoot(const std::filesystem::path& root, const FsDir::Vec& fs)
{
    data_ = std::make_shared<Data>();
    data_->root_ = root;

    for (const auto& node : fs)
    {
        data_->fs_.push_back(node.data_);
    }
}

appbox::LoaderConfig appbox::test::FsRoot::Build() const
{
    appbox::LoaderConfig config;
    std::filesystem::create_directories(data_->root_);

    auto fs_sz = data_->fs_.size();
    for (size_t i = 0; i < fs_sz; ++i)
    {
        data_->fs_[i]->Build(data_->root_);

        auto path_w = (data_->root_ / BuildName(data_->fs_[i]->name_)).wstring();
        auto path_c = appbox::WideToUTF8(path_w);

        if (i == 0)
        {
            config.overlay_fs = path_c;
        }
        else
        {
            config.base_fs.push_back(path_c);
        }
    }

    return config;
}

bool appbox::test::FsRoot::Verify(size_t index, size_t n) const
{
    n = min(n, data_->fs_.size());
    for (size_t i = index; i < n; ++i)
    {
        if (!data_->fs_[i]->Verify(data_->root_))
        {
            return false;
        }
    }
    return true;
}

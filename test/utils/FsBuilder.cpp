#include <fstream>
#include <algorithm>
#include <vector>
#include "utils/ReadFileFull.hpp"
#include "utils/SandboxDll.hpp"
#include "SandboxLayout.hpp"
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

/**
 * @brief Whether a directory entry belongs to the fixed layout of a case.
 *
 * A case declares the content of its directories, but not the entries which
 * the harness writes into them: the isolation files which the helpers of the
 * suite create in the domain directories and the sandbox injection modules
 * which WriteSandboxModules() links into the resource root. The verification
 * skips those entries while it counts the content of a directory, so a case
 * only has to declare what it really describes.
 *
 * @param[in] name Name of the entry.
 * @return true when the entry is not part of the declared content.
 */
static bool IsLayoutEntry(const std::wstring& name)
{
    return name == appbox::layout::kIsolationFileNameW || name == appbox::layout::kSandbox32DllNameW ||
           name == appbox::layout::kSandbox64DllNameW;
}

/**
 * @brief Put the sandbox injection modules into the resource root of a case.
 *
 * The modules are resources of a packed archive: the launcher injects them from
 * `app` and keeps no copy of its own, so every case which starts the launcher
 * needs the real modules in its resource root. They are linked instead of
 * being copied, because a case only needs the files to be there and the
 * modules are megabytes large.
 *
 * @param[in] root Root directory of the case.
 * @return true when both modules are in place, false when the run provided no
 *         modules or they cannot be placed.
 */
static bool WriteSandboxModules(const std::filesystem::path& root)
{
    if (!appbox::test::SandboxModulesAvailable())
    {
        return false;
    }

    const std::filesystem::path app(root / appbox::layout::kAppDirNameW);
    std::error_code             ec;
    std::filesystem::create_directories(app, ec);
    if (ec)
    {
        return false;
    }

    const std::pair<std::wstring, const wchar_t*> modules[] = {
        { appbox::test::Sandbox32DllPath(), appbox::layout::kSandbox32DllNameW },
        { appbox::test::Sandbox64DllPath(), appbox::layout::kSandbox64DllNameW },
    };

    for (const auto& module : modules)
    {
        const auto destination = app / module.second;

        ec.clear();
        std::filesystem::remove(destination, ec);

        ec.clear();
        std::filesystem::create_hard_link(module.first, destination, ec);
        if (ec)
        {
            /* A destination on another volume cannot be linked, so it is copied. */
            ec.clear();
            std::filesystem::copy_file(module.first, destination, std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec)
            {
                return false;
            }
        }
    }

    return true;
}

/**
 * @brief Count the entries a case declared below a directory.
 * @param[in] root Directory to scan.
 * @return The number of entries which are not part of the fixed layout.
 */
static size_t CountFiles(const std::filesystem::path& root)
{
    size_t count = 0;
    for (const auto& entry : std::filesystem::directory_iterator(root))
    {
        if (IsLayoutEntry(entry.path().filename().wstring()))
        {
            continue;
        }
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

appbox::LauncherConfig appbox::test::FsRoot::Build() const
{
    std::filesystem::create_directories(data_->root_);

    for (const auto& node : data_->fs_)
    {
        node->Build(data_->root_);
    }

    /* The modules every case needs, see WriteSandboxModules(). */
    WriteSandboxModules(data_->root_);

    /*
     * The layout of the sandbox is a fixed convention which the case spells
     * out itself, so the launcher configuration carries no path at all: the
     * launcher resolves the state root and the resource root against the
     * directory of its configuration file, which is the working directory of
     * the case.
     */
    return appbox::LauncherConfig{};
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

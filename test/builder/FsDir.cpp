#include "FsBuilder.hpp"
#include "FsDir.hpp"

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

appbox::test::FsDir::FsDir(const std::wstring& name) : FsNode(name)
{
}

void appbox::test::FsDir::Build(const std::filesystem::path& root) const
{
    auto path = root / GetName();
    std::filesystem::create_directories(path);

    for (auto& child : this->children_)
    {
        child->Build(path);
    }
}

bool appbox::test::FsDir::Verify(const std::filesystem::path& root) const
{
    auto path = root / GetName();
    if (!std::filesystem::exists(path))
    {
        return false;
    }

    if (CountFiles(path) != this->children_.size())
    {
        return false;
    }

    for (const auto& child : children_)
    {
        if (!child->Verify(path))
        {
            return false;
        }
    }
    return true;
}

appbox::test::FsNode::Ptr appbox::test::FsDir::Make(const std::wstring& name, const PtrVec& children)
{
    auto dir = std::make_shared<FsDir>(name);
    dir->children_ = children;
    return dir;
}

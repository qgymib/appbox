#include <CLI/Encoding.hpp>
#include <algorithm>
#include "FsBuilder.hpp"

struct appbox::test::FsRoot::Data
{
    std::filesystem::path        root_;
    appbox::test::FsNode::PtrVec fs_;
};

appbox::LoaderConfig appbox::test::FsRoot::Build() const
{
    appbox::LoaderConfig config;
    std::filesystem::create_directories(data_->root_);

    auto fs_sz = data_->fs_.size();
    for (size_t i = 0; i < fs_sz; ++i)
    {
        data_->fs_[i]->Build(data_->root_);

        auto path_w = (data_->root_ / data_->fs_[i]->GetName()).wstring();
        auto path_c = CLI::narrow(path_w);

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
    n = std::min(n, data_->fs_.size());
    for (size_t i = index; i < n; ++i)
    {
        if (!data_->fs_[i]->Verify(data_->root_))
        {
            return false;
        }
    }
    return true;
}

appbox::test::FsRoot::Ptr appbox::test::FsRoot::Make(const std::filesystem::path& root, const FsNode::PtrVec& fs)
{
    appbox::test::FsRoot::Ptr obj(new appbox::test::FsRoot);
    obj->data_ = std::make_shared<FsRoot::Data>();
    obj->data_->root_ = root;
    obj->data_->fs_ = fs;

    return obj;
}

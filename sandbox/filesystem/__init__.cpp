#include "utils/MappingAsDosNtPath.hpp"
#include "utils/CopyFileNt.hpp"
#include "WString.hpp"
#include "Sandbox.hpp"
#include "__init__.hpp"

static void FixComponents(appbox::OverlayFS::ComponentVec& vec, std::wstring& ads)
{
    if (vec.empty())
    {
        return;
    }

    /* Fix ADS */
    {
        auto name = vec[vec.size() - 1];
        auto pos = name.find(L":");
        if (pos != std::wstring::npos)
        {
            vec[vec.size() - 1] = std::wstring(name, 0, pos);
            ads = std::wstring(name, pos + 1);
        }
    }
}

appbox::OverlayFS::Data::Data()
{
    this->has_trailing_slash = false;
}

appbox::OverlayFS::Data::Data(const Data& orig)
{
    this->root = orig.root;
    this->components = orig.components;
    this->has_trailing_slash = orig.has_trailing_slash;
    this->ads = orig.ads;
}

appbox::OverlayFS::OverlayFS(const std::wstring& path)
{
    std::wstring dos_nt_path;
    if (!appbox::MappingAsDosNtPath(path, dos_nt_path))
    {
        return;
    }

    auto data = std::make_shared<Data>();

    /* Split DOS style NT path into components. The first component should be empty. */
    auto comp = appbox::Split(dos_nt_path, L"\\");
    if (comp[0] != L"" || comp.size() < 2)
    {
        return;
    }
    /* Check trailing slash */
    if (comp[comp.size() - 1] == L"")
    {
        data->has_trailing_slash = true;
        comp.pop_back();
    }
    else
    {
        data->has_trailing_slash = false;
    }

    data->root = comp[1];
    if (comp.size() > 2)
    {
        data->components.assign(comp.begin() + 2, comp.end());
    }
    FixComponents(data->components, data->ads);

    this->data_ = data;
}

appbox::OverlayFS::OverlayFS(const OverlayFS& orig)
{
    this->data_ = orig.data_;
}

appbox::OverlayFS::OverlayFS(OverlayFS&& orig) noexcept
{
    this->data_ = orig.data_;
    orig.data_.reset();
}

appbox::OverlayFS& appbox::OverlayFS::operator=(const OverlayFS& orig)
{
    this->data_ = orig.data_;
    return *this;
}

appbox::OverlayFS& appbox::OverlayFS::operator=(OverlayFS&& orig)
{
    this->data_ = orig.data_;
    orig.data_.reset();
    return *this;
}

bool appbox::OverlayFS::IsValid() const
{
    return data_.get() != nullptr;
}

appbox::OverlayFS appbox::OverlayFS::Parent() const
{
    OverlayFS fs;

    if (this->data_->components.size() > 1)
    {
        fs.data_ = std::make_shared<Data>();
        fs.data_->root = this->data_->root;
        /* Parent always directory and has no ads */
        fs.data_->has_trailing_slash = false;
        /* Remove last component */
        fs.data_->components = this->data_->components;
        fs.data_->components.pop_back();
        /* Parent always is a directory */
        fs.data_->has_trailing_slash = true;
    }

    return fs;
}

appbox::OverlayFS appbox::OverlayFS::operator/(const std::wstring& name) const
{
    OverlayFS fs;
    if (this->IsValid() && name.find(L"\\") == std::wstring::npos)
    {
        fs.data_ = std::make_shared<Data>(*this->data_);
        fs.data_->components.push_back(name);
        fs.data_->has_trailing_slash = false;
    }
    return fs;
}

std::wstring appbox::OverlayFS::RebasePath(const std::wstring& base_path) const
{
    std::wstring path = base_path;
    for (const auto& comp : data_->components)
    {
        path += L"\\" + comp;
        if (path[path.size() - 1] == L':')
        {
            path.pop_back();
        }
    }

    if (!data_->ads.empty())
    {
        path += L":" + data_->ads;
    }

    if (data_->has_trailing_slash)
    {
        path += L"\\";
    }

    return path;
}

std::wstring appbox::OverlayFS::RebasePath(const std::string& base_path) const
{
    auto w_base_path = appbox::UTF8ToWide(base_path);
    return RebasePath(w_base_path);
}

std::wstring appbox::OverlayFS::Path() const
{
    std::wstring path;
    if (!IsValid())
    {
        return path;
    }

    path = L"\\" + data_->root;
    for (const auto& comp : data_->components)
    {
        path += L"\\" + comp;
    }

    if (!data_->ads.empty())
    {
        path += L":" + data_->ads;
    }

    if (data_->has_trailing_slash)
    {
        path += L"\\";
    }

    return path;
}

#include <spdlog/spdlog.h>
#include "Random.hpp"
#include "WString.hpp"
#include "__init__.hpp"

appbox::test::CWD::~CWD()
{
    if (cleanup_)
    {
        std::filesystem::remove_all(cwd_);
    }
}

appbox::test::CWD::Ptr appbox::test::CWD::Create(const std::wstring& id, bool cleanup)
{
    Ptr obj(new CWD);

    obj->cleanup_ = cleanup;
    obj->cwd_ = std::filesystem::current_path();
    obj->cwd_ = obj->cwd_ / fmt::format("{}-{}", appbox::WideToUTF8(id), appbox::RandomString(16));

    std::filesystem::create_directories(obj->cwd_);
    return obj;
}

std::wstring appbox::test::CWD::WString() const
{
    return cwd_.wstring();
}

void appbox::test::CWD::NoCleanup(bool yes)
{
    this->cleanup_ = !yes;
}

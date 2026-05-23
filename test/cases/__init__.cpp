#include <spdlog/spdlog.h>
#include <gtest/gtest.h>
#include <chrono>
#include "WString.hpp"
#include "Test.hpp"
#include "__init__.hpp"

appbox::test::CWD::~CWD()
{
    bool no_cleanup = no_cleanup_ || appbox::test::config.no_cleanup;
    if (!no_cleanup)
    {
        std::filesystem::remove_all(cwd_);
    }
}

static std::string GetTimestampMs()
{
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    return std::to_string(ms);
}

appbox::test::CWD::Ptr appbox::test::CWD::Create()
{
    Ptr obj(new CWD);

    auto info = ::testing::UnitTest::GetInstance()->current_test_info();
    auto name = info->test_suite_name() + std::string(".") + info->name();
    auto name_w = appbox::UTF8ToWide(name);

    obj->no_cleanup_ = false;
    obj->cwd_ = std::filesystem::current_path();
    obj->cwd_ = obj->cwd_ / fmt::format("Test-{}-{}", appbox::WideToUTF8(name_w), GetTimestampMs());

    std::filesystem::create_directories(obj->cwd_);
    return obj;
}

std::wstring appbox::test::CWD::WString() const
{
    return cwd_.wstring();
}

void appbox::test::CWD::NoCleanup(bool yes)
{
    this->no_cleanup_ = yes;
}

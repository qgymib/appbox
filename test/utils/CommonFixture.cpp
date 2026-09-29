#include "utils/CWD.hpp"
#include "utils/SandboxDll.hpp"
#include "CommonFixture.hpp"

struct appbox::test::CommonFixture::Data
{
    appbox::test::CWD::Ptr cwd_;
};

appbox::test::CommonFixture::CommonFixture()
{
    data_ = new Data;
    data_->cwd_ = appbox::test::CWD::Create();
}

void appbox::test::CommonFixture::SetUp()
{
    if (!appbox::test::SandboxModulesAvailable())
    {
        GTEST_SKIP() << "the sandbox injection modules are unavailable: pass --sandbox32= and --sandbox64=";
    }
}

appbox::test::CommonFixture::~CommonFixture()
{
    data_->cwd_->NoCleanup(HasFailure());
    delete data_;
}

std::filesystem::path appbox::test::CommonFixture::GetCWD() const
{
    return data_->cwd_->cwd_;
}

std::wstring appbox::test::CommonFixture::GetCWDString() const
{
    return data_->cwd_->WString();
}

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <gtest/gtest.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "probe/__init__.hpp"
#include "loader/Config.hpp"
#include "WString.hpp"
#include "__init__.hpp"

struct TestCreateFileW : testing::Test
{
    TestCreateFileW()
    {
        cwd_ = appbox::test::CWD::Create(L"TestCreateFileW");
    }

    ~TestCreateFileW() override
    {
        cwd_->NoCleanup(HasFailure());
    }

    appbox::test::CWD::Ptr cwd_;
    appbox::LoaderConfig   config_;
};

struct ProbeCreateFileW
{
    static constexpr char* Method = "CreateFileW";

    struct Req
    {
        std::string FileName;
        DWORD       dwDesiredAccess = GENERIC_READ;
        DWORD       dwShareMode = 0;
        DWORD       dwCreationDisposition = OPEN_EXISTING;
        DWORD       dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Req, FileName, dwDesiredAccess, dwShareMode, dwCreationDisposition,
                                                    dwFlagsAndAttributes)
    };

    struct Rsp
    {
        DWORD code = 0; /* Error code. */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, code)
    };

    static nlohmann::json Entry(const nlohmann::json& data)
    {
        auto req = data.get<Req>();

        auto wFileName = appbox::UTF8ToWide(req.FileName);
        auto hFile = CreateFileW(wFileName.c_str(), req.dwDesiredAccess, req.dwShareMode, nullptr,
                                 req.dwCreationDisposition, req.dwFlagsAndAttributes, nullptr);

        Rsp rsp;
        if (hFile == INVALID_HANDLE_VALUE)
        {
            rsp.code = GetLastError();
        }

        CloseHandle(hFile);
        return rsp;
    }
};

static appbox::test::Probe probe(ProbeCreateFileW::Method, ProbeCreateFileW::Entry);

TEST_F(TestCreateFileW, NewFile)
{
    ProbeCreateFileW::Req req;

    auto file_path = cwd_->cwd_ / "TestCreateFileW.json";
    req.FileName = appbox::WideToUTF8(file_path.wstring());
    req.dwCreationDisposition = CREATE_ALWAYS;
    req.dwDesiredAccess = GENERIC_WRITE;

    auto rsp = probe.Call(req, cwd_->WString(), config_).get<ProbeCreateFileW::Rsp>();
    ASSERT_EQ(rsp.code, 0);

    /*
     * The file should exist in the sandbox.
     * So original file path should not exist.
     */
    ASSERT_FALSE(std::filesystem::exists(file_path));
}

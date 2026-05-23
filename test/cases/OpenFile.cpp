#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <gtest/gtest.h>
#include <filesystem>
#include <spdlog/spdlog.h>
#include "probe/__init__.hpp"
#include "loader/Config.hpp"
#include "WString.hpp"
#include "__init__.hpp"
#include "Test.hpp"

struct OpenFile : testing::Test
{
    OpenFile()
    {
        cwd_ = appbox::test::CWD::Create();
    }

    ~OpenFile() override
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

TEST_F(OpenFile, NewFile)
{
    auto file_path = cwd_->cwd_ / "CreateFile_NewFile";

    /* Create file in sandbox */
    {
        ProbeCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(file_path.wstring());
        req.dwCreationDisposition = CREATE_ALWAYS;
        req.dwDesiredAccess = GENERIC_WRITE;

        auto rsp = probe.Call(req, cwd_->WString(), config_).get<ProbeCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }

    /*
     * The file should exist in the sandbox.
     * So original file path should not exist.
     */
    ASSERT_FALSE(std::filesystem::exists(file_path));

    /* Open file in sandbox */
    {
        ProbeCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(file_path.wstring());
        req.dwCreationDisposition = OPEN_EXISTING;
        req.dwDesiredAccess = GENERIC_READ;

        auto rsp = probe.Call(req, cwd_->WString(), config_).get<ProbeCreateFileW::Rsp>();
        ASSERT_EQ(rsp.code, 0);
    }
}

TEST_F(OpenFile, NonExist)
{
    auto file_path = cwd_->cwd_ / "CreateFile_NonExist";

    {
        ProbeCreateFileW::Req req;
        req.FileName = appbox::WideToUTF8(file_path.wstring());
        req.dwCreationDisposition = OPEN_EXISTING;

        auto rsp = probe.Call(req, cwd_->WString(), config_).get<ProbeCreateFileW::Rsp>();
        ASSERT_NE(rsp.code, 0);
    }
}

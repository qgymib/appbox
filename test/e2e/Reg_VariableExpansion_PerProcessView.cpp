#include "probe/RegReadValues.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/ReadFileFull.hpp"
#include "utils/TestKnownFolder.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <spdlog/fmt/fmt.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Whether a byte buffer carries a UTF-16 text.
 *
 * The data of a registry value is stored in the hive file as it is written, so
 * a text which the sandbox keeps in the hive is part of the file.
 *
 * @param[in] bytes Content of the hive file.
 * @param[in] text Text to look for.
 * @return true when the file carries the text.
 */
bool CarriesText(const std::vector<uint8_t>& bytes, const std::wstring& text)
{
    if (text.empty() || bytes.size() < text.size() * sizeof(wchar_t))
    {
        return false;
    }

    const auto* needle = reinterpret_cast<const uint8_t*>(text.data());
    return std::search(bytes.begin(), bytes.end(), needle, needle + text.size() * sizeof(wchar_t)) != bytes.end();
}

} // namespace

/**
 * Condition:
 * 1. The sandboxed application of the 32 bit probe writes the value `Path` with
 *    the text `%APPBOX:ProgramFiles%\Foo`, so the hive of the sandbox holds the
 *    reference and not an expanded path.
 * 2. The 32 bit probe reads the value in a run of its own.
 * 3. The 64 bit probe reads the same value in a further run of the same state
 *    directory.
 *
 * Expected:
 * 1. The 32 bit process reads `C:\Program Files (x86)\Foo`, which is the folder
 *    its own table names for the reference.
 * 2. The 64 bit process reads `C:\Program Files\Foo`: the expansion belongs to
 *    the process which reads the value and not to the process which mounted the
 *    hive first, and the state hive keeps the reference of the application.
 * 3. The real registry never gains the key, so the whole exchange stays in the
 *    hive of the sandbox.
 */
TEST_F(E2E_Reg, VariableExpansion_PerProcessView)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    const auto launcher_config = tree.Build();

    const std::wstring programs_64 = GetKnownFolderPath(L"#ProgramFiles#", false);
    const std::wstring programs_86 = GetKnownFolderPath(L"#ProgramFilesX86#", false);

    const std::wstring subkey =
        L"Software\\AppBoxTest\\VariableExpansionPerProcessView_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::wstring reference = L"%APPBOX:ProgramFiles%\\Foo";

    /* The application stores the reference as it is spelled in the workspace. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Path";
        req.Data = appbox::WideToUTF8(reference);

        const auto rsp = Probe(GetCWD(), launcher_config, ProbeBitness::Bit32).Call<ProtocolRegWriteValue>(req);

        const std::string report = fmt::format("write: create={} set={} read={} readback='{}'", rsp.create_code,
                                               rsp.set_code, rsp.query_code, rsp.readback);

        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS)) << report;

        /* The process which wrote the value reads the folder of its own view. */
        EXPECT_EQ(rsp.readback, appbox::WideToUTF8(programs_86 + L"\\Foo")) << report;
    }

    /* The 32 bit process of a later run reads the folder of its own view. */
    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Path" };

        const auto rsp = Probe(GetCWD(), launcher_config, ProbeBitness::Bit32).Call<ProtocolRegReadValues>(req);

        ASSERT_EQ(rsp.values.size(), req.Values.size());
        ASSERT_EQ(rsp.values[0].query_code, static_cast<DWORD>(ERROR_SUCCESS));

        EXPECT_EQ(rsp.values[0].text, appbox::WideToUTF8(programs_86 + L"\\Foo"));
    }

    /*
     * The 64 bit process reads the folder of its own view: the first process of
     * the run did not decide the expansion of the state hive.
     */
    {
        ProtocolRegReadValues::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Values = { "Path" };

        const auto rsp = Probe(GetCWD(), launcher_config).Call<ProtocolRegReadValues>(req);

        ASSERT_EQ(rsp.values.size(), req.Values.size());
        ASSERT_EQ(rsp.values[0].query_code, static_cast<DWORD>(ERROR_SUCCESS));

        EXPECT_EQ(rsp.values[0].text, appbox::WideToUTF8(programs_64 + L"\\Foo"));
    }

    /* The hive of the state directory keeps the reference of the application. */
    {
        const auto state_hive = GetCWD() / appbox::layout::kStateDirNameW / appbox::layout::kRegistryDirNameW /
                                appbox::layout::kRegistryHiveFileNameW;

        std::vector<uint8_t> bytes;
        ASSERT_EQ(ReadFileFull(state_hive.wstring(), bytes), static_cast<DWORD>(0));
        EXPECT_TRUE(CarriesText(bytes, reference)) << "the state hive does not carry the reference any more";
    }

    /* The real registry must not contain the key. */
    {
        HKEY       key = nullptr;
        const auto result = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key);
        EXPECT_NE(result, ERROR_SUCCESS);
        if (result == ERROR_SUCCESS)
        {
            RegCloseKey(key);
        }
    }
}

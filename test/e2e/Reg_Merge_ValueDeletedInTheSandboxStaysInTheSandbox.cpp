#include "probe/RegDeleteValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/ProbeCall.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Build the raw data of a `REG_SZ` value.
 * @param[in] text Text of the value.
 * @return The UTF-16 data with its trailing terminator.
 */
std::vector<BYTE> StringData(const std::wstring& text)
{
    std::vector<BYTE> data((text.size() + 1) * sizeof(wchar_t), 0);
    memcpy(data.data(), text.c_str(), text.size() * sizeof(wchar_t));
    return data;
}

} // namespace

/**
 * Condition:
 * 1. The key is marked `Merge` and holds the value `Shared` in the real HKCU and
 *    in the sandbox hive, so the sandbox holds the value.
 * 2. The sandboxed process deletes the value `Shared` and writes it again with
 *    another data.
 *
 * Expected:
 * 1. The delete removes the value of the sandbox and records the deletion, so the
 *    read back reports `File Not Found` although the host holds a value of that
 *    name: the value of the host stays hidden.
 * 2. The write lands in the sandbox, because a value the sandbox deleted counts
 *    as held: the read back reports the data of the sandbox.
 * 3. The value of the real registry keeps the data of the host: neither the
 *    delete nor the write of the sandbox reached it.
 */
TEST_F(E2E_Reg, Merge_ValueDeletedInTheSandboxStaysInTheSandbox)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\Merge_DeletedInTheSandbox_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"Shared", L"host"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"Shared", REG_SZ, StringData(L"hive"));
    builder.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Merge);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegDeleteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Shared";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegDeleteValue>(req);
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

        for (const auto& name : rsp.names)
        {
            EXPECT_NE(name, "Shared");
        }
    }

    /* The write after the delete lands in the sandbox again. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "Shared";
        req.Data = "again";

        const auto rsp = Probe(GetCWD(), config).Call<ProtocolRegWriteValue>(req);
        ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "again");
    }

    /* Neither the delete nor the write reached the real registry. */
    EXPECT_EQ(host.GetString(L"Shared"), L"host");
}

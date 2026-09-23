#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

namespace
{

/**
 * @brief Read a `REG_SZ` value of a key of the real HKCU.
 *
 * @param[in] subkey Path of the key below HKCU.
 * @param[in] name Name of the value.
 * @param[out] value The text of the value, empty on failure.
 * @return `ERROR_SUCCESS`, or the failure of the open / query.
 */
LONG ReadHostString(const std::wstring& subkey, const std::wstring& name, std::wstring& value)
{
    value.clear();

    HKEY key = nullptr;
    const LONG open = RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key);
    if (open != ERROR_SUCCESS)
    {
        return open;
    }

    wchar_t    buffer[256] = {};
    DWORD      size = sizeof(buffer);
    const LONG query = RegQueryValueExW(key, name.c_str(), nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size);
    RegCloseKey(key);

    if (query == ERROR_SUCCESS)
    {
        value = buffer;
    }
    return query;
}

} // namespace

/**
 * Condition:
 * 1. The key `HostOnly` exists in the real HKCU with the value `HostValue`, the
 *    hive does not hold it and the isolation file does not mention it, so it
 *    keeps the default mode `WriteCopy`.
 * 2. The key `HiveHeld` exists in the sandbox hive only.
 * 3. The key `FullKey` exists in the real HKCU and is marked `Full`, so the
 *    host entry is invisible for the sandbox.
 * 4. The key `NewKey` exists in neither layer.
 * 5. Create every one of those keys inside the sandbox (which reports the
 *    disposition the caller observes).
 *
 * Expected:
 * 1. The create of `HostOnly` reports `REG_OPENED_EXISTING_KEY`: the merged
 *    view of `WriteCopy` holds the key, even though the hive layer only just
 *    created the shadow key. A caller which initializes a key it believes to
 *    be new must not write defaults over the data of the host key.
 * 2. The create of `HiveHeld` reports `REG_OPENED_EXISTING_KEY` as well: the
 *    hive wins the merged view.
 * 3. The create of `FullKey` reports `REG_CREATED_NEW_KEY`: `Full` keeps the
 *    host entry invisible, so the key is new for the sandbox.
 * 4. The create of `NewKey` reports `REG_CREATED_NEW_KEY`.
 * 5. The host keys keep their own value and never receive the value of the
 *    sandbox.
 */
TEST_F(Reg, WriteCopy_CreateDisposition)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto        prefix = L"Software\\AppBoxTest\\WriteCopy_CreateDisposition_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string host_text = "HostValue";
    const std::string sandbox_text = "SandboxValue";

    const auto host_only = prefix + L"\\HostOnly";
    const auto hive_held = prefix + L"\\HiveHeld";
    const auto full_key  = prefix + L"\\FullKey";
    const auto new_key   = prefix + L"\\NewKey";

    /* The host holds two of the keys, the hive holds one and the isolation
     * file marks one of the host keys `Full`. */
    RealHkcuKey host_only_key(host_only);
    ASSERT_NE(host_only_key.get(), nullptr);
    ASSERT_TRUE(host_only_key.SetString(L"HostValue", appbox::UTF8ToWide(host_text)));

    RealHkcuKey full_key_host(full_key);
    ASSERT_NE(full_key_host.get(), nullptr);
    ASSERT_TRUE(full_key_host.SetString(L"HostValue", appbox::UTF8ToWide(host_text)));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.EnsureKey(L"HKEY_CURRENT_USER\\" + hive_held);
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + full_key, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* The host holds the key and the hive does not: the key exists in the
     * merged view of `WriteCopy`. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key   = appbox::WideToUTF8(host_only);
        req.Value = "TestValue";
        req.Data  = sandbox_text;

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_OPENED_EXISTING_KEY));
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.readback, sandbox_text);
    }

    /* The hive holds the key: the hive wins the merged view, so the key exists
     * as well. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key   = appbox::WideToUTF8(hive_held);
        req.Value = "TestValue";
        req.Data  = sandbox_text;

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_OPENED_EXISTING_KEY));
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.readback, sandbox_text);
    }

    /* The host entry of a `Full` key is invisible, so the key is new for the
     * sandbox. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key   = appbox::WideToUTF8(full_key);
        req.Value = "TestValue";
        req.Data  = sandbox_text;

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_CREATED_NEW_KEY));
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.readback, sandbox_text);
    }

    /* Neither layer holds the key: the create builds it inside the hive. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key   = appbox::WideToUTF8(new_key);
        req.Value = "TestValue";
        req.Data  = sandbox_text;

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        EXPECT_EQ(rsp.disposition, static_cast<DWORD>(REG_CREATED_NEW_KEY));
        ASSERT_EQ(rsp.query_code, 0u);
        EXPECT_EQ(rsp.readback, sandbox_text);
    }

    /* The host keys are unchanged: the value of the sandbox landed in the
     * shadow key of the hive. */
    std::wstring host_value;
    ASSERT_EQ(ReadHostString(host_only, L"HostValue", host_value), ERROR_SUCCESS);
    EXPECT_EQ(host_value, appbox::UTF8ToWide(host_text));
    EXPECT_NE(ReadHostString(host_only, L"TestValue", host_value), ERROR_SUCCESS);

    ASSERT_EQ(ReadHostString(full_key, L"HostValue", host_value), ERROR_SUCCESS);
    EXPECT_EQ(host_value, appbox::UTF8ToWide(host_text));
    EXPECT_NE(ReadHostString(full_key, L"TestValue", host_value), ERROR_SUCCESS);
}

#include "probe/RegReadValue.hpp"
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
 * 1. The key exists in the real HKCU with the value `host` and in the sandbox
 *    hive with the value `sandbox`; the isolation file marks the key `Full`.
 * 2. Read the value inside the sandbox.
 *
 * Expected:
 * 1. The read returns the value of the hive: the virtual registry of the
 *    sandbox wins over the host.
 * 2. The value of the host registry is unchanged.
 */
TEST_F(Reg, Full_HiveWinsOverReal)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Full_HiveWinsOverReal_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string hive_data = "sandbox";

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"TestValue", L"host"));

    /* The hive holds the same key with its own value. */
    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"TestValue", REG_SZ, StringData(L"sandbox"));
    builder.SetKeyIsolation(L"HKEY_CURRENT_USER\\" + subkey, appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegReadValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "TestValue";

    const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    ASSERT_EQ(rsp.open_code, 0u);
    ASSERT_EQ(rsp.query_code, 0u);
    EXPECT_EQ(rsp.data, hive_data);

    /* The host value is still the one the test wrote. */
    HKEY  key = nullptr;
    DWORD type = 0;
    wchar_t buffer[64] = {};
    DWORD size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"TestValue", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}

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
 * 1. The key exists in the real HKCU with the values `RealOnly` and `Shared`,
 *    the sandbox hive holds the same key with the value `Shared` only and the
 *    isolation file marks the value `RealOnly` of the host as `Full`.
 * 2. Read both values inside the sandbox.
 *
 * Expected:
 * 1. `Shared` is answered by the hive.
 * 2. `RealOnly` reports that it does not exist, although the host holds it,
 *    because a single value can be isolated as well.
 */
TEST_F(Reg, Full_HidesRealValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Full_HidesRealValue_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"RealOnly", L"host"));
    ASSERT_TRUE(real_key.SetString(L"Shared", L"host"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"Shared", REG_SZ, StringData(L"sandbox"));
    builder.SetValueIsolation(L"HKEY_CURRENT_USER\\" + subkey, L"RealOnly", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegReadValue::Req shared;
    shared.Key   = appbox::WideToUTF8(subkey);
    shared.Value = "Shared";

    const auto shared_rsp = ProbeRegReadValue.Call(shared, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    ASSERT_EQ(shared_rsp.open_code, 0u);
    ASSERT_EQ(shared_rsp.query_code, 0u);
    EXPECT_EQ(shared_rsp.data, "sandbox");

    ProtocolRegReadValue::Req hidden;
    hidden.Key   = appbox::WideToUTF8(subkey);
    hidden.Value = "RealOnly";

    const auto hidden_rsp = ProbeRegReadValue.Call(hidden, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
    EXPECT_EQ(hidden_rsp.open_code, 0u);
    EXPECT_EQ(hidden_rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* Both values of the host registry are unchanged. */
    HKEY  key = nullptr;
    DWORD type = 0;
    wchar_t buffer[64] = {};
    DWORD size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"RealOnly", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
    RegCloseKey(key);
}

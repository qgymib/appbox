#include "probe/RegDeleteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <algorithm>

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
 * 1. The key exists in the real HKCU with the value `Shared`, and the sandbox
 *    hive holds the same key with its own `Shared` value, so the hive layer
 *    answers the value (the hive wins the merged view).
 * 2. Delete `Shared` inside the sandbox.
 *
 * Expected:
 * 1. The value of the hive is removed.
 * 2. The value stays gone: the read reports `ERROR_FILE_NOT_FOUND` and the
 *    merged value enumeration does not list the name, because the delete
 *    recorded the value of the host as deleted as well.
 * 3. The real registry keeps its own value.
 */
TEST_F(Reg, DeleteValue_ShadowValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\DeleteValue_ShadowValue_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"Shared", L"host"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"Shared", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    ProtocolRegDeleteValue::Req req;
    req.Key   = appbox::WideToUTF8(subkey);
    req.Value = "Shared";

    const auto rsp = ProbeRegDeleteValue.Call(req, GetCWD(), config).get<ProtocolRegDeleteValue::Rsp>();
    ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(rsp.delete_code, static_cast<DWORD>(ERROR_SUCCESS));

    /* The value of the hive is gone and the host value does not reappear. */
    EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(rsp.enum_code, static_cast<DWORD>(ERROR_SUCCESS));
    EXPECT_EQ(std::count(rsp.names.begin(), rsp.names.end(), "Shared"), 0);

    /* The host registry keeps its value. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"Shared", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size), ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}

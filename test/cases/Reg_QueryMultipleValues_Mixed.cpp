#include "probe/RegQueryMultipleValues.hpp"
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
 * 1. The key exists in the real HKCU with the values `HostA` and `HostB`; the
 *    sandbox hive holds the same key with the value `Sandbox`, and the
 *    isolation file marks the host value `HostB` as `Full`.
 * 2. Query several values in one call inside the sandbox.
 *
 * Expected:
 * 1. A batch which mixes both layers is answered completely: the value of the
 *    hive and the visible value of the host carry their own data and type.
 * 2. A batch which only names hive values is answered as well.
 * 3. A batch which names a value the isolation hides fails as a whole with
 *    `ERROR_FILE_NOT_FOUND`, which is what the kernel reports for a missing
 *    value of a batch.
 * 4. The real registry is unchanged.
 */
TEST_F(Reg, QueryMultipleValues_Mixed)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\QueryMultipleValues_Mixed_" + appbox::UTF8ToWide(appbox::RandomString(8));

    RealHkcuKey real_key(subkey);
    ASSERT_NE(real_key.get(), nullptr);
    ASSERT_TRUE(real_key.SetString(L"HostA", L"host-a"));
    ASSERT_TRUE(real_key.SetString(L"HostB", L"host-b"));

    HiveBuilder builder(GetCWD() / L"Upper");
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"Sandbox", REG_SZ, StringData(L"sandbox"));
    builder.SetValueIsolation(L"HKEY_CURRENT_USER\\" + subkey, L"HostB", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    /* A batch which mixes the hive layer and the visible host layer. */
    {
        ProtocolRegQueryMultipleValues::Req req;
        req.Key   = appbox::WideToUTF8(subkey);
        req.Names = {"Sandbox", "HostA"};

        const auto rsp = ProbeRegQueryMultipleValues.Call(req, GetCWD(), config)
                             .get<ProtocolRegQueryMultipleValues::Rsp>();
        ASSERT_EQ(rsp.open_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.types.size(), 2u);
        ASSERT_EQ(rsp.values.size(), 2u);
        EXPECT_EQ(rsp.types[0], static_cast<DWORD>(REG_SZ));
        EXPECT_EQ(rsp.types[1], static_cast<DWORD>(REG_SZ));
        EXPECT_EQ(rsp.values[0], "sandbox");
        EXPECT_EQ(rsp.values[1], "host-a");
        EXPECT_GT(rsp.total_size, 0u);
    }

    /* A batch which only names hive values. */
    {
        ProtocolRegQueryMultipleValues::Req req;
        req.Key   = appbox::WideToUTF8(subkey);
        req.Names = {"Sandbox"};

        const auto rsp = ProbeRegQueryMultipleValues.Call(req, GetCWD(), config)
                             .get<ProtocolRegQueryMultipleValues::Rsp>();
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.values.size(), 1u);
        EXPECT_EQ(rsp.values[0], "sandbox");
    }

    /* A batch which names a hidden host value fails as a whole. */
    {
        ProtocolRegQueryMultipleValues::Req req;
        req.Key   = appbox::WideToUTF8(subkey);
        req.Names = {"HostA", "HostB"};

        const auto rsp = ProbeRegQueryMultipleValues.Call(req, GetCWD(), config)
                             .get<ProtocolRegQueryMultipleValues::Rsp>();
        EXPECT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    }

    /* The real registry keeps its values. */
    HKEY    key = nullptr;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_QUERY_VALUE, &key), ERROR_SUCCESS);
    ASSERT_EQ(RegQueryValueExW(key, L"HostA", nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer), &size), ERROR_SUCCESS);
    RegCloseKey(key);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host-a");
}

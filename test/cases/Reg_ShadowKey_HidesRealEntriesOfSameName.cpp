#include "probe/RegEnumKey.hpp"
#include "probe/RegEnumValue.hpp"
#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <set>
#include <string>

typedef appbox::test::CommonFixture Reg;
using namespace appbox::test;

/**
 * @brief Delete the key of the test from the real HKCU.
 */
struct RealKeyGuard
{
    std::wstring subkey;

    ~RealKeyGuard()
    {
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey.c_str());
    }
};

/**
 * @brief Create a key of the real registry and write a REG_SZ value into it.
 *
 * The key is created outside the sandbox, so it exists in the real HKCU only.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] value The value name.
 * @param[in] data The value data.
 */
static void WriteRealValue(const std::wstring& subkey, const wchar_t* value, const std::string& data)
{
    HKEY key = nullptr;
    ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr),
              ERROR_SUCCESS);

    auto wdata = appbox::UTF8ToWide(data);
    ASSERT_EQ(RegSetValueExW(key, value, 0, REG_SZ, reinterpret_cast<const BYTE*>(wdata.c_str()),
                             static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t))),
              ERROR_SUCCESS);
    RegCloseKey(key);
}

/**
 * @brief Read a REG_SZ value of a real key outside the sandbox.
 *
 * @param[in] subkey The key path relative to HKCU.
 * @param[in] value The value name.
 * @return The value data.
 */
static std::string ReadRealValue(const std::wstring& subkey, const wchar_t* value)
{
    HKEY key = nullptr;
    EXPECT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);

    wchar_t buf[128] = {};
    DWORD   buf_size = sizeof(buf);
    EXPECT_EQ(RegQueryValueExW(key, value, nullptr, nullptr, reinterpret_cast<LPBYTE>(buf), &buf_size), ERROR_SUCCESS);
    RegCloseKey(key);
    return appbox::WideToUTF8(buf);
}

/**
 * Condition:
 * 1. The real HKCU holds the key with the REG_SZ value "TestValue" = "host"
 *    and the sub key "Conflict" with the REG_SZ value "Marker" = "host".
 * 2. Inside the sandbox the key is created (a shadow in the hive), the same
 *    named value "TestValue" = "sandbox" is written into the shadow, and the
 *    same named sub key "Conflict" (with "Marker" = "sandbox") is created
 *    below the shadow.
 * 3. The values and the sub keys of the shadow are enumerated inside the
 *    sandbox and the marker of the shadowed sub key is read back.
 *
 * Expected:
 * 1. The hive layer wins the conflict: the value enumeration and the sub key
 *    enumeration report "TestValue" and "Conflict" once each, and the read
 *    back of "TestValue" returns the data of the hive layer.
 * 2. The read of "Conflict\Marker" inside the sandbox returns the data of the
 *    hive layer, so the real sub key of the same name is answered from the
 *    hive.
 * 3. The real registry keeps its own "TestValue" and "Marker".
 *
 * The merged enumeration drops the real entry of a name the hive layer holds
 * on purpose: a name is answered by the layer in front (the hive) and appears
 * once, which is the same rule the directory merge of the filesystem
 * isolation applies to a name which an upper layer holds (see "Isolation
 * modes" of docs/RegistryIsolation.md). The entries of the real key which the
 * hive does not shadow stay visible through the read through and the merged
 * enumeration.
 */
TEST_F(Reg, ShadowKey_HidesRealEntriesOfSameName)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\ShadowKey_HidesRealEntriesOfSameName_"
                        + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto conflict = subkey + L"\\Conflict";

    const std::string host_data = "host";
    const std::string sandbox_data = "sandbox";

    /* The real registry holds the key, the value and the sub key of the test. */
    WriteRealValue(subkey, L"TestValue", host_data);
    WriteRealValue(conflict, L"Marker", host_data);

    RealKeyGuard guard{ subkey };

    /* Create the shadow of the key and write the same named value into it. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "TestValue";
        req.Data = sandbox_data;
        auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        ASSERT_EQ(rsp.readback, sandbox_data);
    }

    /* Create the same named sub key inside the hive and write its marker. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(conflict);
        req.Value = "Marker";
        req.Data = sandbox_data;
        auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        ASSERT_EQ(rsp.readback, sandbox_data);
    }

    /* The value enumeration sees one "TestValue", the one of the hive layer. */
    {
        ProtocolRegEnumValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        auto rsp = ProbeRegEnumValue.Call(req, GetCWD(), config).get<ProtocolRegEnumValue::Rsp>();
        ASSERT_EQ(rsp.open_code, 0u);
        ASSERT_EQ(rsp.enum_code, 0u);

        std::set<std::string> names(rsp.names.begin(), rsp.names.end());
        ASSERT_EQ(names.size(), 1u);
        ASSERT_TRUE(names.count("TestValue") > 0);
        ASSERT_EQ(rsp.values["TestValue"], sandbox_data);
    }

    /* The sub key enumeration sees one "Conflict", the one of the hive layer. */
    {
        ProtocolRegEnumKey::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        auto rsp = ProbeRegEnumKey.Call(req, GetCWD(), config).get<ProtocolRegEnumKey::Rsp>();
        ASSERT_EQ(rsp.open_code, 0u);
        ASSERT_EQ(rsp.count_code, 0u);
        ASSERT_EQ(rsp.enum_code, 0u);

        std::set<std::string> names(rsp.names.begin(), rsp.names.end());
        ASSERT_EQ(names.size(), 1u);
        ASSERT_TRUE(names.count("Conflict") > 0);
        ASSERT_EQ(rsp.subkey_count, 1u);
    }

    /* The shadowed sub key answers from the hive. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(conflict);
        req.Value = "Marker";
        auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        ASSERT_EQ(rsp.open_code, 0u);
        ASSERT_EQ(rsp.query_code, 0u);
        ASSERT_EQ(rsp.data, sandbox_data);
    }

    /* The real entries of the same name are untouched. */
    ASSERT_EQ(ReadRealValue(subkey, L"TestValue"), host_data);
    ASSERT_EQ(ReadRealValue(conflict, L"Marker"), host_data);
}

#include "probe/RegEnumValue.hpp"
#include "probe/RegWriteValue.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
#include <set>

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
 * Condition:
 * 1. The real HKCU holds the key with the REG_SZ value "RealValue".
 * 2. Inside the sandbox the key is created (a shadow in the hive) and the
 *    REG_SZ value "SandboxValue" is written into the shadow.
 * 3. The values are enumerated inside the sandbox and every enumerated value
 *    is read back.
 *
 * Expected:
 * 1. The enumeration sees the merged view: "SandboxValue" of the hive layer
 *    and "RealValue" of the real registry.
 * 2. Both values are readable: the hive value from the hive and the real
 *    value through the read through of NtQueryValueKey.
 * 3. The real registry does not hold "SandboxValue".
 */
TEST_F(Reg, EnumerateValue_Merged)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto       subkey = L"Software\\AppBoxTest\\EnumerateValue_Merged_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const std::string real_data = "RealData";
    const std::string sandbox_data = "SandboxData";

    /* Create the key and the real value outside the sandbox. */
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                  nullptr),
                  ERROR_SUCCESS);

        auto wdata = appbox::UTF8ToWide(real_data);
        ASSERT_EQ(RegSetValueExW(key, L"RealValue", 0, REG_SZ, reinterpret_cast<const BYTE*>(wdata.c_str()),
                                 static_cast<DWORD>((wdata.size() + 1) * sizeof(wchar_t))),
                  ERROR_SUCCESS);
        RegCloseKey(key);
    }

    RealKeyGuard guard{ subkey };

    /* Create the shadow and write the sandbox value inside the sandbox. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "SandboxValue";
        req.Data = sandbox_data;
        auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
        ASSERT_EQ(rsp.set_code, 0u);
        ASSERT_EQ(rsp.readback, sandbox_data);
    }

    /* Enumerate the values inside the sandbox: merged view. */
    {
        ProtocolRegEnumValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        auto rsp = ProbeRegEnumValue.Call(req, GetCWD(), config).get<ProtocolRegEnumValue::Rsp>();
        ASSERT_EQ(rsp.open_code, 0u);
        ASSERT_EQ(rsp.enum_code, 0u);

        std::set<std::string> names(rsp.names.begin(), rsp.names.end());
        ASSERT_EQ(names.size(), 2u);
        ASSERT_TRUE(names.count("SandboxValue") > 0);
        ASSERT_TRUE(names.count("RealValue") > 0);

        /* Every enumerated value must be readable with its own data. */
        ASSERT_EQ(rsp.values.size(), 2u);
        ASSERT_EQ(rsp.values["SandboxValue"], sandbox_data);
        ASSERT_EQ(rsp.values["RealValue"], real_data);
    }

    /* The real registry does not hold the sandbox value. */
    {
        HKEY key = nullptr;
        ASSERT_EQ(RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key), ERROR_SUCCESS);
        ASSERT_EQ(RegQueryValueExW(key, L"SandboxValue", nullptr, nullptr, nullptr, nullptr), ERROR_FILE_NOT_FOUND);
        RegCloseKey(key);
    }
}

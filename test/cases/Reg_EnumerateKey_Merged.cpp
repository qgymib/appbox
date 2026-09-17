#include "probe/RegEnumKey.hpp"
#include "probe/RegQueryKeyName.hpp"
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
 * @brief Collect the sub key names of a real key outside the sandbox.
 * @param[in] subkey The key path relative to HKCU.
 * @return The sub key names.
 */
static std::set<std::wstring> EnumerateRealSubKeys(const std::wstring& subkey)
{
    std::set<std::wstring> names;
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, subkey.c_str(), 0, KEY_READ, &key) == ERROR_SUCCESS)
    {
        for (DWORD i = 0;; ++i)
        {
            wchar_t name[256] = {};
            DWORD   len = (DWORD)std::size(name);
            if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS)
            {
                break;
            }
            names.insert(name);
        }
        RegCloseKey(key);
    }
    return names;
}

/**
 * Condition:
 * 1. The real HKCU holds the sub keys "RealA" and "RealB" below the test key.
 * 2. Inside the sandbox the test key is created (a shadow in the hive) and
 *    the sub key "SandboxC" is created below it.
 * 3. The sub keys are enumerated inside the sandbox.
 *
 * Expected:
 * 1. The enumeration sees the merged view: "SandboxC" of the hive layer and
 *    "RealA" and "RealB" of the real registry.
 * 2. RegQueryInfoKeyW() reports the merged sub key count.
 * 3. The real registry still holds only "RealA" and "RealB".
 */
TEST_F(Reg, EnumerateKey_Merged)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"Upper")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\EnumerateKey_Merged_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto sandbox_child = appbox::WideToUTF8(subkey) + "\\SandboxC";

    /* Create the real sub keys "RealA" and "RealB" outside the sandbox. */
    {
        HKEY key = nullptr;
        for (const auto* child : {L"RealA", L"RealB"})
        {
            std::wstring path = subkey + L"\\" + child;
            ASSERT_EQ(RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key,
                                      nullptr),
                      ERROR_SUCCESS);
            RegCloseKey(key);
        }
    }

    RealKeyGuard guard{ subkey };

    /* Create the shadow of the test key inside the sandbox. */
    {
        ProtocolRegQueryKeyName::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        auto rsp = ProbeRegQueryKeyName.Call(req, GetCWD(), config).get<ProtocolRegQueryKeyName::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
    }

    /* Create the sandbox only sub key "SandboxC" inside the sandbox. */
    {
        ProtocolRegWriteValue::Req req;
        req.Key = sandbox_child;
        req.Value = "Marker";
        req.Data = "Sandbox";
        auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, 0u);
    }

    /* Enumerate the sub keys inside the sandbox: merged view. */
    {
        ProtocolRegEnumKey::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        auto rsp = ProbeRegEnumKey.Call(req, GetCWD(), config).get<ProtocolRegEnumKey::Rsp>();
        ASSERT_EQ(rsp.open_code, 0u);
        ASSERT_EQ(rsp.enum_code, 0u);
        ASSERT_EQ(rsp.count_code, 0u);

        std::set<std::string> names(rsp.names.begin(), rsp.names.end());
        ASSERT_EQ(names.size(), 3u);
        ASSERT_TRUE(names.count("SandboxC") > 0);
        ASSERT_TRUE(names.count("RealA") > 0);
        ASSERT_TRUE(names.count("RealB") > 0);

        /* RegQueryInfoKeyW() must report the merged count as well. */
        ASSERT_EQ(rsp.subkey_count, 3u);
    }

    /* The real registry still holds only the two real sub keys. */
    {
        auto names = EnumerateRealSubKeys(subkey);
        ASSERT_EQ(names.size(), 2u);
        ASSERT_TRUE(names.count(L"RealA") > 0);
        ASSERT_TRUE(names.count(L"RealB") > 0);
    }
}

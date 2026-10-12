#include "probe/RegReadValue.hpp"
#include "probe/RegWriteValue.hpp"
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
 * 1. The key exists in the real HKCU with the value `HostValue` and the sandbox
 *    hive holds the same key with the value `HiveValue`. The key is marked
 *    `Merge`, so the sandbox holds the key and the host holds the value which
 *    the sandbox does not hold.
 * 2. The sandboxed process writes the value `HostValue` and reads it back.
 *
 * Expected:
 * 1. The write reaches the real registry: the host value carries the data of the
 *    sandbox, because `Merge` is the only mode whose write lands in the host for
 *    an entry the sandbox does not hold.
 * 2. The read back of the sandbox reports the same data: the value lives in the
 *    host layer, which the merged view shows.
 * 3. The value of the hive is untouched.
 */
TEST_F(E2E_Reg, Merge_WritesTheHostValue)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey =
        L"Software\\AppBoxTest\\Merge_WritesTheHostValue_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"HostValue", L"host"));

    HiveBuilder builder(GetCWD());
    builder.SetValue(hive_key, L"HiveValue", REG_SZ, StringData(L"hive"));
    builder.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Merge);

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    {
        ProtocolRegWriteValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "HostValue";
        req.Data = "sandbox";

        const auto rsp = ProbeRegWriteValue.Call(req, GetCWD(), config).get<ProtocolRegWriteValue::Rsp>();
        ASSERT_EQ(rsp.create_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.set_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.readback, "sandbox");
    }

    /* The write reached the value of the real registry. */
    EXPECT_EQ(host.GetString(L"HostValue"), L"sandbox");

    /* The value of the hive is untouched. */
    {
        ProtocolRegReadValue::Req req;
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "HiveValue";

        const auto rsp = ProbeRegReadValue.Call(req, GetCWD(), config).get<ProtocolRegReadValue::Rsp>();
        ASSERT_EQ(rsp.query_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.data, "hive");
    }
}

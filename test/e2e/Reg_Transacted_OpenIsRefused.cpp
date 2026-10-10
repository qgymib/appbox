#include "probe/RegTransacted.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "Random.hpp"
#include "WString.hpp"
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
 * 1. The sandbox hive holds the key with the value `SandboxValue`.
 * 2. Open the key read only inside a transaction of the probe, once through
 *    `NtOpenKeyTransacted` and once through `NtOpenKeyTransactedEx`.
 *
 * Expected:
 * 1. Both calls are refused with `STATUS_NOT_SUPPORTED`: the hive layer is the
 *    only layer which may answer a transacted open of a key of the view, and it
 *    cannot answer one because an application hive does not support
 *    transactions. The isolation never falls back to the host layer, because
 *    the handle of the host layer would enlist the real hive into the
 *    transaction of the sandboxed process.
 * 2. The view keeps working: the key is still readable with the value of the
 *    sandbox through the plain open, which is the merged view of the
 *    isolation.
 */
TEST_F(E2E_Reg, Transacted_OpenIsRefused)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data")
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\Transacted_Open_" + appbox::UTF8ToWide(appbox::RandomString(8));

    HiveBuilder builder(GetCWD());
    builder.SetValue(L"HKEY_CURRENT_USER\\" + subkey, L"SandboxValue", REG_SZ, StringData(L"sandbox"));

    std::string error;
    ASSERT_TRUE(builder.Write(error)) << error;

    for (const char* api : { "open", "open_ex" })
    {
        ProtocolRegTransacted::Req req;
        req.Api = api;
        req.Access = "read";
        req.Key = appbox::WideToUTF8(subkey);
        req.Value = "SandboxValue";
        req.End = "close";

        const auto rsp = ProbeRegTransacted.Call(req, GetCWD(), config).get<ProtocolRegTransacted::Rsp>();
        ASSERT_EQ(rsp.mount_code, static_cast<DWORD>(ERROR_SUCCESS));
        ASSERT_EQ(rsp.tx_code, static_cast<DWORD>(ERROR_SUCCESS));
        EXPECT_EQ(rsp.call_code, static_cast<DWORD>(STATUS_NOT_SUPPORTED)) << "api: " << api;

        /* The key of the sandbox is still part of the view. */
        EXPECT_EQ(rsp.after_open_code, static_cast<DWORD>(ERROR_SUCCESS)) << "api: " << api;
        EXPECT_EQ(rsp.after_read_code, static_cast<DWORD>(ERROR_SUCCESS)) << "api: " << api;
        EXPECT_EQ(rsp.after, "sandbox") << "api: " << api;
    }
}

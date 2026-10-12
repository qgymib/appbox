#include "probe/RegReadValues.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/PatchBuilder.hpp"
#include "utils/RealHkcuKey.hpp"
#include "Random.hpp"
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include <filesystem>
#include <string>
#include <vector>

typedef appbox::test::CommonFixture E2E_Patch;
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

/**
 * @brief Write a patch package into the patch directory of the case.
 * @param[in] cwd Working directory of the case.
 * @param[in] name File name of the package.
 * @param[in] registry Registry domain of the package.
 * @return true on success.
 */
bool WritePackage(const std::filesystem::path& cwd, const wchar_t* name, const PatchRegistry& registry)
{
    std::error_code ec;
    const auto      directory = cwd / appbox::layout::kPatchDirNameW;
    std::filesystem::create_directories(directory, ec);

    return WritePatchPackage(directory / name, {}, {}, registry);
}

} // namespace

/**
 * Condition:
 * 1. The real HKCU holds the key with the values `HostA` and `HostB`.
 * 2. The archive holds the key with the values `Own` and `Packed`; its isolation
 *    file marks the key `Merge`, so the values of the host stay visible, and the
 *    value `Packed` `Hide`, so the content of the value lives in the hidden
 *    store of the hive.
 * 3. `01-bar.zip` marks the key `Full` and the value `Packed` `Full`, while it
 *    does not name `HostA` and `HostB`.
 * 4. The sandboxed process reads the four values of the key.
 *
 * Expected:
 * 1. `HostA` and `HostB` do not exist: the mode of the package hides the key of
 *    the host with everything below it, which is the mode of the last layer.
 * 2. `Own` is the value of the archive, because the hive holds the key.
 * 3. `Packed` is the value of the archive as well: the mode of the package makes
 *    the content visible which the archive hid.
 * 4. The real key of the host is untouched.
 */
TEST_F(E2E_Patch, RegistryIsolationModeOfTheLastLayerWins)
{
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"app", { FsDir(L"filesystem", {}) })
    });
    /* clang-format on */

    auto config = tree.Build();

    const auto subkey = L"Software\\AppBoxTest\\PatchRegistryIsolation_" + appbox::UTF8ToWide(appbox::RandomString(8));
    const auto hive_key = L"HKEY_CURRENT_USER\\" + subkey;

    /* The key and its two values belong to the host. */
    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"HostA", L"host"));
    ASSERT_TRUE(host.SetString(L"HostB", L"host"));

    /*
     * The archive holds the key as well, so the sandbox opens it inside the
     * hive and the modes of the layers decide about the values of the host.
     */
    HiveBuilder app(GetCWD());
    app.SetValue(hive_key, L"Own", REG_SZ, StringData(L"archive"));
    app.SetValue(hive_key, L"Packed", REG_SZ, StringData(L"packed"));
    app.SetKeyIsolation(hive_key, appbox::RegistryKeyIsolation::Merge);
    app.SetValueIsolation(hive_key, L"Packed", appbox::RegistryValueIsolation::Hide);

    std::string error;
    ASSERT_TRUE(app.Write(error)) << error;

    RegistryKeyIsolationEntry key_entry;
    key_entry.key_path = hive_key;
    key_entry.isolation = appbox::RegistryKeyIsolation::Full;

    RegistryValueIsolationEntry value_entry;
    value_entry.key_path = hive_key;
    value_entry.value_name = L"Packed";
    value_entry.isolation = appbox::RegistryValueIsolation::Full;

    PatchRegistry bar;
    bar.key_isolation.push_back(key_entry);
    bar.value_isolation.push_back(value_entry);
    ASSERT_TRUE(WritePackage(GetCWD(), L"01-bar.zip", bar));

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "HostA", "HostB", "Own", "Packed" };

    const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
    ASSERT_EQ(rsp.values.size(), 4u);

    /* The mode of the package hides the values of the host key. */
    EXPECT_EQ(rsp.values[0].query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));
    EXPECT_EQ(rsp.values[1].query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The values of the archive stay visible. */
    EXPECT_EQ(rsp.values[2].query_code, 0u);
    EXPECT_EQ(rsp.values[2].text, "archive");

    /* The mode of the package makes the hidden content of the archive visible. */
    EXPECT_EQ(rsp.values[3].query_code, 0u);
    EXPECT_EQ(rsp.values[3].text, "packed");

    /* The real key of the host keeps its values. */
    DWORD   type = 0;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegQueryValueExW(host.get(), L"HostA", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}

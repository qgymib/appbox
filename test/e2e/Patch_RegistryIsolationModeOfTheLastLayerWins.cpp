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
 * 1. The hive of the archive holds the key with a value of its own, the real
 *    HKCU holds the same key with the three values `ByThePackage`,
 *    `OfTheArchive` and `ByTheArchive`.
 * 2. The isolation file of the archive keeps `ByThePackage` visible, hides
 *    `OfTheArchive` and hides `ByTheArchive`.
 * 3. `01-bar.zip` hides `ByThePackage` and keeps `ByTheArchive` visible, while
 *    it does not name `OfTheArchive`.
 * 4. The sandboxed process reads the three values of the key.
 *
 * Expected:
 * 1. `ByThePackage` does not exist, because the mode of the package overrides
 *    the mode the archive set for the value.
 * 2. `OfTheArchive` does not exist either, because a value no file of a layer
 *    above the archive names keeps the mode of the archive.
 * 3. `ByTheArchive` is the value of the host, because the mode of the package
 *    makes it visible again.
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

    /* The key and its three values belong to the host. */
    RealHkcuKey host(subkey);
    ASSERT_NE(host.get(), nullptr);
    ASSERT_TRUE(host.SetString(L"ByThePackage", L"host"));
    ASSERT_TRUE(host.SetString(L"OfTheArchive", L"host"));
    ASSERT_TRUE(host.SetString(L"ByTheArchive", L"host"));

    /*
     * The archive holds the key as well, so the sandbox opens it inside the
     * hive and the value modes of the merged view decide about the values of
     * the host.
     */
    HiveBuilder app(GetCWD());
    app.SetValue(hive_key, L"Own", REG_SZ, StringData(L"archive"));
    app.SetValueIsolation(hive_key, L"ByThePackage", appbox::RegistryIsolation::WriteCopy);
    app.SetValueIsolation(hive_key, L"OfTheArchive", appbox::RegistryIsolation::Full);
    app.SetValueIsolation(hive_key, L"ByTheArchive", appbox::RegistryIsolation::Full);

    std::string error;
    ASSERT_TRUE(app.Write(error)) << error;

    PatchRegistry bar;
    bar.isolation.push_back(RegistryIsolationEntry{ hive_key, L"ByThePackage", true, appbox::RegistryIsolation::Full });
    bar.isolation.push_back(
        RegistryIsolationEntry{ hive_key, L"ByTheArchive", true, appbox::RegistryIsolation::WriteCopy });
    ASSERT_TRUE(WritePackage(GetCWD(), L"01-bar.zip", bar));

    ProtocolRegReadValues::Req req;
    req.Key = appbox::WideToUTF8(subkey);
    req.Values = { "ByThePackage", "OfTheArchive", "ByTheArchive" };

    const auto rsp = ProbeRegReadValues.Call(req, GetCWD(), config).get<ProtocolRegReadValues::Rsp>();
    ASSERT_EQ(rsp.values.size(), 3u);

    /* The mode of the package hides a value the archive kept visible. */
    EXPECT_EQ(rsp.values[0].query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* A value no package names keeps the mode of the archive. */
    EXPECT_EQ(rsp.values[1].query_code, static_cast<DWORD>(ERROR_FILE_NOT_FOUND));

    /* The mode of the package makes a value of the host visible again. */
    EXPECT_EQ(rsp.values[2].query_code, 0u);
    EXPECT_EQ(rsp.values[2].text, "host");

    /* The real key of the host keeps its values. */
    DWORD   type = 0;
    wchar_t buffer[64] = {};
    DWORD   size = sizeof(buffer);
    ASSERT_EQ(RegQueryValueExW(host.get(), L"ByTheArchive", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size),
              ERROR_SUCCESS);
    EXPECT_EQ(appbox::WideToUTF8(buffer), "host");
}

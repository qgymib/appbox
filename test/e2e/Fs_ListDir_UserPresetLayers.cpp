#include "probe/ListDir.hpp"
#include "utils/CommonFixture.hpp"
#include "utils/FsBuilder.hpp"
#include "utils/TestKnownFolder.hpp"
#include <algorithm>
#include <CLI/Encoding.hpp>

typedef appbox::test::CommonFixture E2E_Fs;
using namespace appbox::test;

namespace
{

/**
 * @brief Whether a listing holds a file of a given name.
 * @param[in] rsp Listing of a directory.
 * @param[in] name Name of the file.
 * @return true when the file is part of the listing.
 */
bool HoldsFile(const ProtocolListDir::Rsp& rsp, const std::string& name)
{
    return std::any_of(rsp.entries.begin(), rsp.entries.end(),
                       [&name](const ProtocolListDir::Rsp::Entry& entry) { return entry.file && entry.name == name; });
}

/**
 * @brief List the host folder behind one layer of the case.
 *
 * The path of the listing is the real folder of the layer key, so the case
 * observes the folder the sandbox maps the layer to.
 *
 * @param[in] layer Layer key of the case, for example `L"#Documents#"`.
 * @param[in] cwd Working directory of the case.
 * @param[in] config Configuration of the case.
 * @return The listing of the folder inside the view of the sandbox.
 */
ProtocolListDir::Rsp ListLayer(const std::wstring& layer, const std::filesystem::path& cwd,
                               appbox::LoaderConfig& config)
{
    ProtocolListDir::Req req;
    req.path = CLI::narrow(GetKnownFolderPath(layer, false));
    req.method = ProtocolListDir::Req::Method::Std;

    ProtocolListDir::Rsp rsp;
    ProbeListDir.Call(req, cwd, config).get_to(rsp);
    return rsp;
}

} // namespace

/**
 * Condition:
 * 1. The resource tree holds a lower layer for the `Documents` folder and one
 *    for the `Desktop` folder of the user, each with a file of its own.
 * 2. The sandboxed process lists both folders.
 *
 * Expected:
 * 1. Every file is listed in the folder of its own layer.
 * 2. A file of the other layer is not listed, so each layer is mapped to the
 *    folder its layer key names.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ListDir_UserPresetLayers)
{
    const std::string  documents_name = "Fs.ListDir_UserPresetLayers.Documents.txt";
    const std::string  desktop_name = "Fs.ListDir_UserPresetLayers.Desktop.txt";
    const std::wstring w_documents_name = CLI::widen(documents_name);
    const std::wstring w_desktop_name = CLI::widen(desktop_name);

    /*
     * Every level declares one directory per entry, because the verification
     * of a case counts the entries of a directory and compares them with the
     * children it declared.
     */
    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", {
                FsDir(L"#Documents#", {
                    FsFile(w_documents_name, "documents")
                }),
                FsDir(L"#Desktop#", {
                    FsFile(w_desktop_name, "desktop")
                })
            })
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    /* The layer of the Documents folder is mapped to the real Documents folder. */
    const auto documents = ListLayer(L"#Documents#", GetCWD(), config);
    EXPECT_TRUE(HoldsFile(documents, documents_name));
    EXPECT_FALSE(HoldsFile(documents, desktop_name));

    /* The layer of the Desktop folder is mapped to the real Desktop folder. */
    const auto desktop = ListLayer(L"#Desktop#", GetCWD(), config);
    EXPECT_TRUE(HoldsFile(desktop, desktop_name));
    EXPECT_FALSE(HoldsFile(desktop, documents_name));

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

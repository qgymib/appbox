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
 * @brief One layer of the case: the layer key and the file it carries.
 */
struct LayerFile
{
    const wchar_t* layer; /* Layer key of the preset directory. */
    const char*    name;  /* Name of the file below the layer. */
};

/**
 * @brief The layers of the case and the file of each one.
 *
 * The file of a nested layer is not an entry of the listing of its parent,
 * because the real folder of the parent holds the folder of the child instead
 * of the files below it.
 */
const LayerFile s_layers[] = {
    { L"#Documents#",       "Fs.ListDir_UserPresetLayers.Documents.txt"       },
    { L"#Desktop#",         "Fs.ListDir_UserPresetLayers.Desktop.txt"         },
    { L"#AppData#",         "Fs.ListDir_UserPresetLayers.AppData.txt"         },
    { L"#LocalAppData#",    "Fs.ListDir_UserPresetLayers.LocalAppData.txt"    },
    { L"#LocalAppDataLow#", "Fs.ListDir_UserPresetLayers.LocalAppDataLow.txt" },
    { L"#Downloads#",       "Fs.ListDir_UserPresetLayers.Downloads.txt"       },
    { L"#Favorites#",       "Fs.ListDir_UserPresetLayers.Favorites.txt"       },
    { L"#Music#",           "Fs.ListDir_UserPresetLayers.Music.txt"           },
    { L"#Pictures#",        "Fs.ListDir_UserPresetLayers.Pictures.txt"        },
    { L"#StartMenu#",       "Fs.ListDir_UserPresetLayers.StartMenu.txt"       },
    { L"#Programs#",        "Fs.ListDir_UserPresetLayers.Programs.txt"        },
    { L"#Startup#",         "Fs.ListDir_UserPresetLayers.Startup.txt"         },
};

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
                               appbox::LauncherConfig& config)
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
 * 1. The resource tree holds a lower layer for every folder of the user below
 *    `Current User Directory` -- `Documents`, `Desktop`, `Application Data`,
 *    `Local Application Data`, `Local Application Data Low`, `Downloads`,
 *    `Favorites`, `Music`, `Pictures`, `Start Menu`, `Programs` and `Startup` --
 *    each with a file of its own.
 * 2. The sandboxed process lists every one of those folders.
 *
 * Expected:
 * 1. Every file is listed in the folder of its own layer.
 * 2. A file of another layer is not listed, so each layer is mapped to the
 *    folder its layer key names. `Programs` and `Startup` hang below the real
 *    Start Menu folder, so their files are not entries of the listing of the
 *    folder above them either.
 * 3. The resources of the application are untouched.
 */
TEST_F(E2E_Fs, ListDir_UserPresetLayers)
{
    /*
     * Every level declares one directory per entry, because the verification
     * of a case counts the entries of a directory and compares them with the
     * children it declared.
     */
    FsNode::Nodes layers;
    for (const auto& entry : s_layers)
    {
        layers.push_back(FsDir(entry.layer, { FsFile(CLI::widen(entry.name), "content") }));
    }

    /* clang-format off */
    auto tree = FsRoot(GetCWD(), {
        FsDir(L"data", {}),
        FsDir(L"app", {
            FsDir(L"filesystem", layers)
        })
    });
    /* clang-format on */

    auto config = tree.Build();

    for (const auto& entry : s_layers)
    {
        const auto listing = ListLayer(entry.layer, GetCWD(), config);

        /* The file of the layer is listed in the real folder of its own key. */
        EXPECT_TRUE(HoldsFile(listing, entry.name)) << entry.name;

        /* The file of no other layer is listed there. */
        for (const auto& other : s_layers)
        {
            if (&other == &entry)
            {
                continue;
            }
            EXPECT_FALSE(HoldsFile(listing, other.name)) << entry.name << " holds " << other.name;
        }
    }

    /* Verify that the resources of the application are untouched. */
    ASSERT_TRUE(tree.Verify());
}

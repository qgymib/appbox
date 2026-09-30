#ifndef APPBOX_TEST_UTILS_PATCH_BUILDER_HPP
#define APPBOX_TEST_UTILS_PATCH_BUILDER_HPP

#include "utils/EnvironmentIsolationBuilder.hpp"
#include "utils/FsIsolationBuilder.hpp"
#include "utils/HiveBuilder.hpp"
#include "utils/NetworkIsolationBuilder.hpp"
#include <filesystem>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief One file a patch package carries.
 */
struct PatchFile
{
    /**
     * @brief Path of the file in the virtual filesystem.
     *
     * The first component is the layer key of a preset directory, for example
     * `L"#USERPROFILE#\\AppBoxTest\\app.txt"`. The helper roots the path in the
     * filesystem domain of the package, like the packer does.
     */
    std::wstring path;

    /**
     * @brief Content of the file.
     */
    std::string content;
};

/**
 * @brief One value of the registry of a patch package.
 */
struct PatchRegistryValue
{
    /**
     * @brief Path of the key which holds the value, from the hive root.
     */
    std::wstring key_path;

    /**
     * @brief Name of the value, empty for the default value of the key.
     */
    std::wstring value_name;

    /**
     * @brief The `REG_*` type code of the value.
     */
    DWORD type = REG_NONE;

    /**
     * @brief Raw data of the value.
     */
    std::vector<BYTE> data;
};

/**
 * @brief Registry domain of a patch package.
 *
 * The domain is written only when the case describes it: a package whose case
 * names no key, no value and no mode carries no resource of the registry at
 * all, which is what a case about a package that keeps the registry of the
 * layers below it needs.
 */
struct PatchRegistry
{
    /**
     * @brief Keys of the hive of the package, created when they hold no value.
     */
    std::vector<std::wstring> keys;

    /**
     * @brief Values of the hive of the package.
     */
    std::vector<PatchRegistryValue> values;

    /**
     * @brief Modes the isolation file of the package lists.
     */
    std::vector<RegistryIsolationEntry> isolation;

    /**
     * @brief Text written as the hive of the package instead of a built hive.
     *
     * A case which pins how the launcher treats a hive it cannot mount writes
     * the bytes itself with this member, for example a file which is not a
     * hive at all.
     */
    std::string raw_hive;

    /**
     * @brief Text written as the isolation file instead of the modes.
     *
     * A case which pins how the launcher and the sandbox treat a document they
     * cannot use writes the text itself with this member, for example a
     * document which is not valid JSON.
     */
    std::string raw_isolation;
};

/**
 * @brief Network domain of a patch package.
 *
 * The domain is written only when the case describes it: a package whose case
 * lists no redirection, configures no proxy and writes no text of its own
 * carries no resource of the network domain at all, which is what a case about
 * a package that keeps the network configuration of the layers below it needs.
 */
struct PatchNetwork
{
    /**
     * @brief DNS redirections of the package.
     */
    std::vector<NetworkIsolationEntry> entries;

    /**
     * @brief Proxy of the package, written only while it carries a value.
     */
    NetworkIsolationProxy proxy;

    /**
     * @brief Text written as the isolation file instead of the entries.
     *
     * A case which pins how the sandbox treats a document it cannot use writes
     * the text itself with this member, for example a document which is not
     * valid JSON.
     */
    std::string raw_isolation;
};

/**
 * @brief Environment domain of a patch package.
 *
 * The domain is written only when the case describes it, like the network
 * domain: a package whose case lists no variable and writes no text of its own
 * carries no resource of the environment domain at all.
 */
struct PatchEnvironment
{
    /**
     * @brief Variables of the package.
     */
    std::vector<EnvironmentIsolationEntry> entries;

    /**
     * @brief Text written as the isolation file instead of the entries.
     *
     * A case which pins how the sandbox treats a document it cannot use writes
     * the text itself with this member, for example a document which is not
     * valid JSON.
     */
    std::string raw_isolation;
};

/**
 * @brief Write a patch package.
 *
 * The package is the product of the `Patch (ZIP)` project type: the resource
 * tree a standalone archive keeps below `app`, rooted at the archive root and
 * without the launcher. The helper writes the resources a case describes, so a
 * case does not need the packer to build a package; the packer writes the very
 * same entry names, which `Unit_PackService` pins.
 *
 * ```
 * filesystem/<layer key>/<path>        the files of the case
 * filesystem/isolation.json            the modes of the case, when it lists one
 * registry/user.hiv                    the registry of the case, when it describes one
 * registry/isolation.json              the registry modes of the case, when it lists one
 * network/isolation.json               the network configuration of the case, when it describes one
 * environment/isolation.json           the variables of the case, when it describes one
 * ```
 *
 * @param[in] zip_path Path of the package to write (truncated when it exists).
 * @param[in] files Files of the filesystem domain, by their virtual path.
 * @param[in] isolation Isolation modes of the filesystem domain; an empty list
 *                      writes no isolation file at all.
 * @param[in] registry Registry domain of the package; a member which the case
 *                     does not describe writes no resource at all.
 * @param[in] network Network domain of the package; a member which the case
 *                    does not describe writes no resource at all.
 * @param[in] environment Environment domain of the package; a member which the
 *                        case does not describe writes no resource at all.
 * @return true on success.
 */
bool WritePatchPackage(const std::filesystem::path& zip_path, const std::vector<PatchFile>& files,
                       const std::vector<FsIsolationEntry>& isolation = {}, const PatchRegistry& registry = {},
                       const PatchNetwork& network = {}, const PatchEnvironment& environment = {});

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_PATCH_BUILDER_HPP

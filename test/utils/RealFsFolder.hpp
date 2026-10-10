#ifndef APPBOX_TEST_UTILS_REAL_FS_FOLDER_HPP
#define APPBOX_TEST_UTILS_REAL_FS_FOLDER_HPP

#include <filesystem>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief RAII helper which owns a folder below a known folder of the host.
 *
 * The folder is created by the test process outside the sandbox and removed
 * again when the helper goes out of scope, so a test which needs an entry of
 * the host filesystem does not leave anything behind. The known folder token
 * is the one the filesystem tree of the cases uses, for example `#USERPROFILE#`.
 */
class RealFsFolder
{
public:
    /**
     * @brief Create a folder below a known folder.
     * @param[in] known_folder Token of the known folder, for example
     *                         `L"#USERPROFILE#"`.
     * @param[in] name Name of the folder below the known folder.
     */
    RealFsFolder(const std::wstring& known_folder, const std::wstring& name);

    ~RealFsFolder();

    RealFsFolder(const RealFsFolder&) = delete;
    RealFsFolder& operator=(const RealFsFolder&) = delete;
    RealFsFolder(RealFsFolder&&) = delete;
    RealFsFolder& operator=(RealFsFolder&&) = delete;

    /**
     * @brief Write a file inside the folder, creating its parent folders.
     * @param[in] relative Path of the file below the folder.
     * @param[in] text Content of the file.
     * @return true on success.
     */
    bool WriteFile(const std::wstring& relative, const std::string& text) const;

    /**
     * @brief Whether a file inside the folder exists.
     * @param[in] relative Path of the file below the folder.
     * @return true when the file exists.
     */
    bool FileExists(const std::wstring& relative) const;

    /**
     * @brief Create a junction inside the folder.
     *
     * A junction is a reparse point of the kind the view resolves itself, and
     * any user who may write the folder can create one: unlike a symbolic link
     * it asks for no privilege, so a case which needs a link of the host does
     * not depend on the machine it runs on. The helper removes the junctions
     * it created before it removes the folder, because removing the folder
     * would otherwise walk into the targets they name.
     *
     * @param[in] relative Path of the junction below the folder.
     * @param[in] target Target the junction names, an absolute Win32 path or a
     *                   path of the object namespace.
     * @return true on success.
     */
    bool CreateJunction(const std::wstring& relative, const std::wstring& target);

    /**
     * @brief Path of the folder.
     * @return The path of the folder.
     */
    const std::filesystem::path& Get() const;

private:
    /**
     * @brief Path of the folder below the known folder.
     */
    std::filesystem::path path_;

    /**
     * @brief Junctions the helper created, removed before the folder itself.
     */
    std::vector<std::filesystem::path> junctions_;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_REAL_FS_FOLDER_HPP

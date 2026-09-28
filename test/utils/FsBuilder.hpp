#ifndef APPBOX_UTILS_FS_BUILDER_HPP
#define APPBOX_UTILS_FS_BUILDER_HPP

#include <string>
#include <vector>
#include <memory>
#include <filesystem>
#include "loader/Config.hpp"

namespace appbox::test
{

struct FsNode
{
    typedef std::vector<uint8_t> Bytes;
    typedef std::vector<FsNode>  Nodes;

    /**
     * @brief Construct directory.
     */
    FsNode(const std::wstring& name, const FsNode::Nodes& children);

    /**
     * @brief Construct file.
     * @param[in] name The name of the file.
     * @param[in] bytes The content of the file.
     */
    FsNode(const std::wstring& name, const Bytes& bytes);

    /**
     * @brief Construct file.
     * @param[in] name The name of the file.
     * @param[in] bytes The content of the file encoding in UTF-8.
     */
    FsNode(const std::wstring& name, const std::string& text);

    struct Data;
    typedef std::shared_ptr<Data> DataPtr;
    typedef std::vector<DataPtr>  DataPtrVec;
    DataPtr                       data_;
};

struct FsFile : FsNode
{
    FsFile(const std::wstring& name, const std::string& text);
};

struct FsDir : FsNode
{
    typedef std::vector<FsDir> Vec;
    FsDir(const std::wstring& name, const FsNode::Nodes& children = {});
};

/**
 * @brief Root of the directories of a case.
 *
 * The builder materializes the directories of the case below the root and
 * re-reads them afterwards. A case spells the layout of the sandbox itself,
 * like the archive of the packer does: the state root `data` carries what the
 * sandbox may modify and the resource root `app` carries what it must not
 * touch, both with the `filesystem` subdirectory which carries their content.
 *
 * The loader configuration carries no path at all: the loader resolves `app`
 * and `data` against the directory of its configuration file, which is the
 * working directory of the case.
 */
struct FsRoot
{
    /**
     * @brief Create a builder for the directories of a case.
     * @param[in] root The root directory, normally the working directory.
     * @param[in] fs The directories of the case, in the order they are built.
     */
    FsRoot(const std::filesystem::path& root, const FsDir::Vec& fs);

    /**
     * @brief Build the directories of the case under the root directory.
     * @return The loader configuration of the case, which carries no path.
     */
    appbox::LoaderConfig Build() const;

    /**
     * @brief Verify the directories of the case.
     *
     * A case which declares the state root first verifies with the default
     * arguments that every resource it declared is untouched.
     *
     * @param[in] index The start index of the directories to verify.
     * @param[in] n The number of directories to verify.
     * @return True if the directories are unchanged.
     */
    bool Verify(size_t index = 1, size_t n = SIZE_MAX) const;

    struct Data;
    std::shared_ptr<Data> data_;
};

} // namespace appbox::test

#endif

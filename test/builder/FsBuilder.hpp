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

struct FsRoot
{
    /**
     * @brief Virtual root that contains one upperfs and multiple lowerfs.
     * @param[in] root The root directory.
     * @param[in] fs The filesystem directories. The first is the upper directory, the others are lower directories.
     */
    FsRoot(const std::filesystem::path& root, const FsDir::Vec& fs);

    /**
     * @brief Build the file system under the root directory.
     * @param[in] root The root directory.
     * @return True if the file system is built successfully.
     */
    appbox::LoaderConfig Build() const;

    /**
     * @brief Verify the file system.
     * @param[in] index The start index of the fs to verify.
     * @param[in] n The number of fs to verify.
     * @return True if the file system is verified successfully.
     */
    bool Verify(size_t index  = 1, size_t n = SIZE_MAX) const;

    struct Data;
    std::shared_ptr<Data> data_;
};

} // namespace appbox::test

#endif

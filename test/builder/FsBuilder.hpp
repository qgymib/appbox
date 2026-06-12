#ifndef APPBOX_UTILS_FS_BUILDER_HPP
#define APPBOX_UTILS_FS_BUILDER_HPP

#include "loader/Config.hpp"
#include "FsDir.hpp"
#include "FsFile.hpp"

namespace appbox::test
{

class FsRoot
{
public:
    typedef std::shared_ptr<FsRoot> Ptr;

    /**
     * @brief Virtual root that contains one upperfs and multiple lowerfs.
     * @param[in] root The root directory.
     * @param[in] fs The filesystem directories. The first is the upper directory, the others are lower directories.
     */
    static Ptr Make(const std::filesystem::path& root, const FsNode::PtrVec& fs);

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
    bool Verify(size_t index = 1, size_t n = SIZE_MAX) const;

    FsRoot() = default;
    FsRoot(const FsRoot&) = delete;
    FsRoot(FsRoot&&) = delete;
    FsRoot& operator=(const FsRoot&) = delete;
    FsRoot& operator=(FsRoot&&) = delete;

private:
    struct Data;
    std::shared_ptr<Data> data_;
};

} // namespace appbox::test

#endif

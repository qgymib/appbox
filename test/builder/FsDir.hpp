#ifndef APPBOX_TEST_BUILDER_FS_DIR_HPP
#define APPBOX_TEST_BUILDER_FS_DIR_HPP

#include "FsNode.hpp"

namespace appbox::test
{

class FsDir : public appbox::test::FsNode
{
public:
    /**
     * @brief Make a directory.
     * @param[in] name The name of the directory.
     * @param[in] children The children of the directory.
     * @return The directory.
     */
    static Ptr Make(const std::wstring& name, const PtrVec& children);

    /**
     * @brief Make a directory.
     * @param[in] name The name of the directory.
     */
    FsDir(const std::wstring& name);
    virtual ~FsDir() = default;

    /**
     * @brief Build directory.
     * @param[in] root The parent directory.
     */
    void Build(const std::filesystem::path& root) const override;

    /**
     * @brief Verify directory contents.
     * @param[in] root The parent directory.
     * @return True if the directory contents are correct.
     */
    bool Verify(const std::filesystem::path& root) const override;

private:
    PtrVec children_;
};

} // namespace appbox::test

#endif

#ifndef APPBOX_TEST_BUILDER_FS_FILE_HPP
#define APPBOX_TEST_BUILDER_FS_FILE_HPP

#include "FsNode.hpp"

namespace appbox::test
{

class FsFile : public appbox::test::FsNode
{
public:
    typedef std::vector<uint8_t> Bytes;

    /**
     * @brief Create a file.
     * @param[in] name The name of the file.
     * @param[in] text The text of the file.
     * @return The file handle.
     */
    static Ptr Make(const std::wstring& name, const std::string& text);

    /**
     * @brief Create a file.
     * @param[in] name The name of the file.
     */
    FsFile(const std::wstring& name);

    /**
     * @brief Create a file.
     * @param[in] name The name of the file.
     * @param[in] text The text of the file.
     */
    FsFile(const std::wstring& name, const std::string& text);

    /**
     * @brief Set the content of the file.
     * @param[in] bytes The content of the file.
     */
    void SetContent(const Bytes& bytes);

    /**
     * @brief Write the file.
     * @param[in] root The parent directory.
     */
    void Build(const std::filesystem::path& root) const override;

    /**
     * @brief Verify the file content.
     * @param[in] root The parent directory.
     * @return True if the file content is correct.
     */
    bool Verify(const std::filesystem::path& root) const override;

private:
    Bytes content;
};

} // namespace appbox::test

#endif

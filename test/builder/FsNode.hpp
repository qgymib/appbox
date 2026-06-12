#ifndef APPBOX_TEST_BUILDER_FS_NODE_HPP
#define APPBOX_TEST_BUILDER_FS_NODE_HPP

#include <string>
#include <vector>
#include <memory>
#include <filesystem>

namespace appbox::test
{

class FsNode
{
public:
    typedef std::shared_ptr<FsNode> Ptr;
    typedef std::vector<Ptr>        PtrVec;

    FsNode(const std::wstring& name);
    virtual ~FsNode() = default;

    /**
     * @brief Get the name of the file system node.
     * @return The name of the file system node.
     */
    const std::wstring& GetName() const;

    virtual void Build(const std::filesystem::path& root) const = 0;
    virtual bool Verify(const std::filesystem::path& root) const = 0;

private:
    std::wstring name;
};

} // namespace appbox::test

#endif

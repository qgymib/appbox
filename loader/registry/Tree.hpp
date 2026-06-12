#ifndef APPBOX_LOADER_REGISTRY_TREE_HPP
#define APPBOX_LOADER_REGISTRY_TREE_HPP

#include <memory>
#include <vector>
#include <filesystem>
#include "Node.hpp"

namespace appbox
{

struct RegistryTree
{
    typedef std::shared_ptr<RegistryTree> Ptr;

    static Ptr Create();

    /**
     * @brief Update the registry tree
     * @param[in] path Path to the registry file
     */
    void Update(const std::filesystem::path& path);

    std::vector<RegistryNode::Ptr> roots;
};

} // namespace appbox

#endif

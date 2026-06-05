#ifndef APPBOX_LOADER_UTILS_REGISTRY_HPP
#define APPBOX_LOADER_UTILS_REGISTRY_HPP

#include <string>
#include <memory>

namespace appbox
{

class RegistryHive
{
public:
    typedef std::shared_ptr<RegistryHive> Ptr;
    struct Data;

    /**
     * @brief Create hive handle.
     * @param[in] path Path to registry hive.
     * @return Object handle.
     */
    static Ptr Create(const std::wstring& path);

    ~RegistryHive();
    RegistryHive(const RegistryHive&) = delete;
    RegistryHive(RegistryHive&&) = delete;
    RegistryHive& operator=(const RegistryHive&) = delete;
    RegistryHive& operator=(RegistryHive&&) = delete;

private:
    RegistryHive();
    Data* data_;
};

} // namespace appbox

#endif

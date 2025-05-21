#ifndef APPBOX_UTILS_META_HPP
#define APPBOX_UTILS_META_HPP

#include <vector>
#include <nlohmann/json.hpp>

namespace appbox
{

enum IsolationMode
{
    ISOLATION_FULL = 0,
    ISOLATION_MERGE = 1,
    ISOLATION_WRITE_COPY = 2,
    ISOLATION_HIDE = 3,
};

enum PayloadType
{
    PAYLOAD_TYPE_NONE = 0,
    PAYLOAD_TYPE_METADATA = 1,
    PAYLOAD_TYPE_FILESYSTEM = 2,
    PAYLOAD_TYPE_REGISTRY = 3,
    PAYLOAD_TYPE_NETWORK = 4,
};

struct PayloadNode
{
    PayloadNode();

    uint8_t  type;      /* #PayloadType. */
    uint8_t  isolation; /* #IsolationMode. */
    uint16_t path_len;  /* Path length in bytes. Path is encoding in UTF-8. */

    /**
     * @brief File attribute;
     * @see https://learn.microsoft.com/en-us/windows/win32/fileio/file-attribute-constants
     */
    uint32_t attribute;
    uint64_t payload_len; /* Binary payload length in bytes. */
};

struct MetaSettingsStartup
{
    std::vector<std::wstring> path; /* Startup file path */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MetaSettingsStartup, path);
};

struct MetaSettings
{
    MetaSettingsStartup startup; /* Startup settings. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MetaSettings, startup);
};

struct Meta
{
    MetaSettings settings; /* Settings. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Meta, settings);
};

inline PayloadNode::PayloadNode()
{
    type = PAYLOAD_TYPE_NONE;
    isolation = 0;
    attribute = 0;
    path_len = 0;
    payload_len = 0;
}

} // namespace appbox

#endif

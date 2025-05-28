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
    PAYLOAD_TYPE_FILESYSTEM = 1, /* filesystem entry. */
    PAYLOAD_TYPE_REGISTRY = 2,   /* Registry entry. */
    PAYLOAD_TYPE_SANDBOX_32 = 3, /* 32-bit sandbox dll. */
    PAYLOAD_TYPE_SANDBOX_64 = 4, /* 64-bit sandbox dll. */
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

struct MetaFile
{
    MetaFile();
    std::string path;      /* File path in sandbox. */
    std::string args;      /* Command line arguments. */
    std::string trigger;   /* Trigger keyword. */
    bool        autoStart; /* Auto startup. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MetaFile, path, args, trigger, autoStart);
};
typedef std::vector<MetaFile> MetaFileVec;

struct MetaSettings
{
    bool        sandboxReset;    /* Reset sandbox after exit. */
    std::string sandboxLocation; /* Sandbox location. */
    MetaFileVec startupFiles;    /* Startup file path */
    MetaSettings();
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MetaSettings, sandboxReset, sandboxLocation,
                                                startupFiles);
};

struct MetaEnvironments
{
    std::string title;   /* Program title. */
    std::string version; /* Program version. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(MetaEnvironments, title);
};

struct Meta
{
    MetaEnvironments environments; /* Environments. */
    MetaSettings     settings;     /* Settings. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Meta, settings);
};

inline MetaFile::MetaFile()
{
    autoStart = false;
}

inline MetaSettings::MetaSettings()
{
    sandboxReset = false;
}

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

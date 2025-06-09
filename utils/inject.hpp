#ifndef APPBOX_UTILS_INJECT_HPP
#define APPBOX_UTILS_INJECT_HPP

#include <list>
#include <nlohmann/json.hpp>
#include "utils/file.hpp"
#include "utils/meta.hpp"

namespace appbox
{

struct InjectFile
{
    std::string   path;       /* File path. */
    IsolationMode isolation;  /* Isolation mode. */
    DWORD         attributes; /* File attributes. */
    InjectFile();
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InjectFile, path, isolation, attributes);
};

struct InjectConfig
{
    std::string           sandboxPath; /* Path to sandbox directory. */
    std::string           dllPath32;   /* Path to 32bit dll. */
    std::string           dllPath64;   /* Path to 64bit dll. */
    std::list<InjectFile> files;       /* Sandbox files. */
    NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(InjectConfig, sandboxPath, dllPath32, dllPath64,
                                                files);
};

inline InjectFile::InjectFile()
{
    isolation = ISOLATION_FULL;
    attributes = 0;
}

} // namespace appbox

#endif

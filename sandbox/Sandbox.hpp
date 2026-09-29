#ifndef APPBOX_SANDBOX_HPP
#define APPBOX_SANDBOX_HPP

#include "utils/WinAPI.h"
#include "utils/PipeClient.hpp"
#include "utils/VariableExpansion.hpp"
#include "environment/Configuration.hpp"
#include "environment/Table.hpp"
#include "filesystem/IsolationTable.hpp"
#include "filesystem/Resolve.hpp"
#include "network/DnsTable.hpp"
#include "Config.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <vector>

namespace appbox
{

namespace network
{

/*
 * The proxy of the sandbox owns the socket API and is only reachable through
 * its own header, so the instance is held through a pointer: the sandbox
 * header is included by every translation unit of the sandbox, and only the
 * ones which carry the socket API may include the winsock 2 headers.
 */
class Proxy;

} // namespace network

struct Sandbox
{
    /**
     * @brief Sandbox isolation enable flag.
     */
    bool bIsolationMode = false;

    /**
     * @brief Whether the environment of the process is composed already.
     *
     * The flag is set for a process a sandboxed application started: the
     * process inherits the environment of its parent, which is the view of the
     * sandbox, so the environment isolation keeps it as it is instead of
     * composing it from the environment of the host a second time.
     */
    bool bEnvironmentComposed = false;

    std::string inject_data;

    /**
     * @brief Named pipe path.
     */
    std::wstring wPipePath;

    /**
     * @brief Filesystem configuration.
     */
    filesystem::ResolveFs fs;

    /**
     * @brief Isolation modes of the virtual filesystem.
     *
     * The table is filled by the filesystem isolation module from the
     * isolation file of the injected configuration. An empty table means that
     * no mode was configured, so every entry keeps the default mode of its
     * kind and the host filesystem stays visible.
     */
    filesystem::IsolationTable fs_isolation;

    /**
     * @brief The variables the sandbox expands in the values of the workspace.
     *
     * The table holds one entry per known folder of the machine which runs the
     * sandbox and is filled from the injected configuration. A value of the
     * Environment or the Registry workspace may reference a folder with
     * `%APPBOX:<NAME>%` instead of spelling its path out: the reference is
     * replaced with the path of the entry while the sandbox composes its
     * environment and while it mounts its registry hive, so the archive stays
     * correct on a machine whose known folders are somewhere else.
     */
    std::vector<VariableMapping> variables;

    /**
     * @brief Registry hive file path (DOS style).
     *
     * Empty when registry isolation is not configured.
     */
    std::wstring wRegistryHiveDOSPath;

    /**
     * @brief Registry isolation file paths (DOS style), in layer order.
     *
     * Every file carries the isolation modes of one layer of the run, the
     * resources of the archive first and the patch packages after them. An
     * empty list or a missing file means that the layer sets no mode, so an
     * entry which no file lists keeps the default mode `Write Copy` and the
     * host registry stays visible. A mode a later file sets for a key or a
     * value overrides the mode of the same entry of the files below it.
     */
    std::vector<std::wstring> wRegistryIsolationDOSPaths;

    /**
     * @brief Filesystem isolation file paths (DOS style), in layer order.
     *
     * Every file carries the isolation modes of one layer of the run, the
     * resources of the archive first and the patch packages after them. An
     * empty list or a missing file means that the layer sets no mode, so an
     * entry which no file lists keeps the default mode of its kind
     * (`Write Copy` for a folder, `Full` for a file) and the host filesystem
     * stays visible. A mode a later file sets for a path overrides the mode
     * of the same path of the files below it.
     */
    std::vector<std::wstring> wFilesystemIsolationDOSPaths;

    /**
     * @brief Network isolation file paths (DOS style), in layer order.
     *
     * Every file carries the network configuration of one layer of the run,
     * the resources of the archive first and the patch packages after them. An
     * empty list or a missing file means that the layer sets no redirection
     * and no proxy, so the configuration of the layers below it stays in
     * place. A redirection a later file lists for a hostname overrides the
     * entry of the same hostname of the files below it, while the proxy of the
     * last file which names a usable one is the proxy of the run.
     */
    std::vector<std::wstring> wNetworkIsolationDOSPaths;

    /**
     * @brief Environment isolation file paths (DOS style), in layer order.
     *
     * Every file carries the environment variables of one layer of the run,
     * the resources of the archive first and the patch packages after them. An
     * empty list or a missing file means that the layer configures no
     * variable, so the environment of the sandbox is composed from the layers
     * below it and from the environment of the host.
     */
    std::vector<std::wstring> wEnvironmentIsolationDOSPaths;

    /**
     * @brief Environment state file path (DOS style).
     *
     * The file carries the modifications the packaged application made to its
     * environment. The sandbox reads it while it composes the environment of
     * the run and sends it back to the loader whenever the application changes
     * a variable, so a modification survives the end of the process which made
     * it.
     */
    std::wstring wEnvironmentStateDOSPath;

    /**
     * @brief The environment the sandboxed process sees.
     *
     * The table is composed by the environment isolation module from the
     * environment of the host, the isolation file of the configuration and the
     * state file of an earlier run, and it answers every entry point which
     * reads, writes, enumerates or expands a variable of the process
     * environment. The environment block of the process itself stays untouched,
     * so the environment of the host is never modified.
     */
    environment::Table env_table;

    /**
     * @brief The modifications the packaged application made.
     *
     * The state is written back to the state file through the RPC method of
     * the loader, so the environment of the next run carries them.
     */
    environment::State env_state;

    /**
     * @brief DNS redirections of the network isolation.
     *
     * The table is filled by the network isolation module from the isolation
     * file of the injected configuration and answers the name resolution of
     * the sandboxed application: a name it knows is answered with the address
     * of its entry instead of being asked at the host.
     */
    network::DnsTable dns_table;

    /**
     * @brief SOCKS5 proxy of the network isolation.
     *
     * The engine is created by the network isolation module from the proxy of
     * the isolation file and carries the traffic of the application through
     * the server the file names. A null instance means that the process has no
     * proxy, so every connection keeps the path of the host.
     */
    std::shared_ptr<network::Proxy> proxy;

    /**
     * @brief Path to 32-bit sandbox dll path. Encoding in UTF-8.
     */
    std::string sandbox32_dos_path;

    /**
     * @brief Path to 64-bit sandbox dll path. Encoding in UTF-8.
     */
    std::string sandbox64_dos_path;

    /**
     * @brief RPC client
     */
    std::shared_ptr<appbox::PipeClient> client;
};
void to_json(nlohmann::json& j, const Sandbox& r);

/**
 * @brief Global sandbox instance
 */
extern Sandbox* sandbox;

} // namespace appbox

#endif

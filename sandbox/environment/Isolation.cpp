#include "utils/WinAPI.h" /* Must be first include file */
#include <cstdint>
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
#include "utils/GetPEB.hpp"
#include "utils/Log.hpp"
#include "utils/VariableExpansion.hpp"
#include "msg/Environment.hpp"
#include "hook/__init__.hpp"
#include "Table.hpp"
#include "Configuration.hpp"
#include "Sandbox.hpp"
#include "WString.hpp"
#include "Isolation.hpp"

namespace
{

/**
 * @brief RtlSetCurrentEnvironment() of ntdll.
 *
 * @see https://ntdoc.m417z.com/rtlsetcurrentenvironment
 */
typedef NTSTATUS(NTAPI* T_RtlSetCurrentEnvironment)(PWSTR Environment, PWSTR* PreviousEnvironment);

/**
 * @brief Read a text file.
 *
 * The module is initialized before the hooks are attached, so the file is read
 * through the original entry points of the process.
 *
 * @param[in] path Path of the file.
 * @param[out] text The text of the file.
 * @return true when the file was read.
 */
bool ReadTextFile(const std::wstring& path, std::string& text)
{
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    text.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return true;
}

/**
 * @brief Apply the variables of one environment isolation file.
 *
 * The value a layer composes is the value the layers below it composed, which
 * is the value the table holds for the variable: the table carries the block
 * of this process before the first layer is applied, so the first layer merges
 * with the value of the host. A variable the table does not hold is absent,
 * exactly like a variable the host does not hold.
 *
 * The composition of a variable reads and writes the table entry of that
 * variable alone, so it does not depend on the variables which were composed
 * before it.
 *
 * The value of a row may reference a known folder of the machine with
 * `%APPBOX:<NAME>%`; the reference is replaced before the value is composed
 * with the value below the layer. A value the application stored at run time
 * is not expanded: it is part of the state and is applied as it is.
 *
 * @param[in] variables The variables of one isolation file.
 */
void ApplyLayer(const std::vector<appbox::environment::ConfiguredVariable>& variables)
{
    for (const auto& variable : variables)
    {
        std::wstring below;
        const bool   below_present = appbox::sandbox->env_table.Get(variable.name, below);

        /*
         * The value the user entered may reference a known folder of this
         * machine with `%APPBOX:<NAME>%`. The reference is replaced before the
         * value is composed with the value below the layer, so the merge joins
         * the expanded text.
         */
        const std::wstring value = appbox::ExpandVariables(variable.value, appbox::sandbox->variables);

        const appbox::environment_isolation::ComposedValue composed =
            appbox::environment_isolation::ComposeEnvironmentValue(below_present, below, value, variable.isolation,
                                                                   variable.merge, variable.merge_string);

        if (!composed.visible)
        {
            appbox::sandbox->env_table.Delete(variable.name);
            continue;
        }

        appbox::sandbox->env_table.Set(variable.name, composed.value);
    }
}

/**
 * @brief Apply the modifications an earlier run made.
 * @param[in] modifications The modifications of the state file.
 */
void ApplyModifications(const std::vector<appbox::environment::Modification>& modifications)
{
    for (const auto& modification : modifications)
    {
        if (modification.deleted)
        {
            appbox::sandbox->env_table.Delete(modification.name);
            continue;
        }

        appbox::sandbox->env_table.Set(modification.name, modification.value);
    }
}

/**
 * @brief Whether the current thread is inside PersistState().
 *
 * The document is built and sent from a hook, so a write the runtime of the
 * application makes while the document is built or sent — the log of a failure
 * writes to a file, for example — would enter the hook again. The nested call
 * returns, and the record it made is part of the document of the outer call,
 * which is built after the record and not before it.
 */
thread_local bool s_persisting = false;

/**
 * @brief Mark the thread as persisting the state while the call is running.
 */
struct PersistGuard
{
    PersistGuard()
    {
        s_persisting = true;
    }

    ~PersistGuard()
    {
        s_persisting = false;
    }

    PersistGuard(const PersistGuard&) = delete;
    PersistGuard& operator=(const PersistGuard&) = delete;
};

/**
 * @brief Send the state of the sandbox to the launcher.
 *
 * The launcher owns the state directory of the sandbox and writes the document
 * the sandbox sends over the RPC pipe. A failure is not fatal: the environment
 * of the running process is the block of the process, which stays correct, and
 * only the next run would miss the modification.
 */
void PersistState()
{
    if (appbox::sandbox == nullptr || appbox::sandbox->client == nullptr)
    {
        return;
    }

    if (s_persisting)
    {
        /* The outer call sends the document which carries this record as well. */
        return;
    }

    PersistGuard guard;

    try
    {
        std::string document;
        std::string error;
        if (!appbox::sandbox->env_state.Build(document, error))
        {
            LOG_W("the environment of the sandbox cannot be kept: {}", error);
            return;
        }

        appbox::MsgEnvironment::Req request;
        request.state = std::move(document);

        nlohmann::json response;
        if (!appbox::sandbox->client->Call(appbox::MsgEnvironment::Method, request, response))
        {
            LOG_W("the launcher did not keep the environment of the sandbox");
        }
    }
    catch (const std::exception& e)
    {
        LOG_W("the environment of the sandbox cannot be kept: {}", e.what());
    }
}

/**
 * @brief Replace the environment block of the process with the composed one.
 *
 * The block of the process parameters is what the loader, the runtime of the
 * application and every module which walks the block itself read, so replacing
 * it is what makes the composed environment the environment of the process.
 * The call keeps the size of the environment in step — the process creation of
 * the operating system copies the block of the parent by that size — and
 * releases the block it replaces, which is why the new block comes from the
 * heap of the process.
 *
 * A failure leaves the process with the block of the host: the sandbox is then
 * transparent for the environment instead of broken, so it is reported and not
 * fatal.
 */
void InstallBlock()
{
    const HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
    {
        LOG_E("the environment of the process is not replaced: ntdll is not loaded");
        return;
    }

    const auto set_current =
        reinterpret_cast<T_RtlSetCurrentEnvironment>(::GetProcAddress(ntdll, "RtlSetCurrentEnvironment"));
    if (set_current == nullptr)
    {
        LOG_E("the environment of the process is not replaced: RtlSetCurrentEnvironment is missing");
        return;
    }

    wchar_t* block = appbox::sandbox->env_table.CreateBlock();
    if (block == nullptr)
    {
        LOG_E("the environment of the process is not replaced: the block is not allocated");
        return;
    }

    const NTSTATUS status = set_current(block, nullptr);
    if (!NT_SUCCESS(status))
    {
        ::HeapFree(::GetProcessHeap(), 0, block);
        LOG_E("the environment of the process is not replaced: {:#x}", static_cast<std::uint32_t>(status));
    }
}

} // namespace

NTSTATUS appbox::environment::Isolation::Init()
{
    if (appbox::sandbox == nullptr || !appbox::sandbox->bIsolationMode)
    {
        /* Nothing to do outside isolation mode; the hooks are not attached. */
        return STATUS_SUCCESS;
    }

    /*
     * The environment of the host is the block of this process: the module is
     * initialized before the hooks are attached, so the block is the real one
     * and the table can be filled from it.
     */
    const LPWCH block = ::GetEnvironmentStringsW();
    if (block != nullptr)
    {
        appbox::sandbox->env_table.AssignBlock(block);
        ::FreeEnvironmentStringsW(block);
    }

    std::size_t configured = 0;

    /*
     * A process which a sandboxed application started inherits the view of its
     * parent: its block is composed already, so the configuration is not
     * applied a second time.
     */
    if (!appbox::sandbox->bEnvironmentComposed)
    {
        /*
         * The environment of the run is composed layer by layer: the file of
         * the resources of the archive comes first and the file of every patch
         * package follows in the order the packages take effect in, and every
         * layer applies its own isolation and merge mode to the value the
         * layers below it composed.
         */
        for (const auto& isolation_path : appbox::sandbox->wEnvironmentIsolationDOSPaths)
        {
            if (isolation_path.empty())
            {
                continue;
            }

            std::string text;
            if (!ReadTextFile(isolation_path, text))
            {
                LOG_D("the environment isolation file does not exist: {}", appbox::WideToUTF8(isolation_path));
                continue;
            }

            std::vector<ConfiguredVariable> variables;
            std::string                     error;
            if (!ParseIsolationDocument(text, variables, error))
            {
                /*
                 * A layer which cannot be read configures no variable, so the
                 * layers below it stay in place.
                 */
                LOG_W("the environment isolation file '{}' is ignored: {}", appbox::WideToUTF8(isolation_path), error);
                continue;
            }

            ApplyLayer(variables);
            configured += variables.size();
        }
    }

    std::size_t modifications = 0;

    /*
     * The state is read by every process of the sandbox: a process which
     * composed its environment applies the modifications of the earlier runs,
     * while a process which inherited the view of its parent only continues the
     * state, because the modifications are part of the block it inherited.
     */
    const std::wstring& state_path = appbox::sandbox->wEnvironmentStateDOSPath;
    if (!state_path.empty())
    {
        std::string text;
        if (ReadTextFile(state_path, text))
        {
            std::string error;
            if (appbox::sandbox->env_state.Parse(text, error))
            {
                if (!appbox::sandbox->bEnvironmentComposed)
                {
                    ApplyModifications(appbox::sandbox->env_state.Entries());
                }
                modifications = appbox::sandbox->env_state.Count();
            }
            else
            {
                LOG_W("the environment state of the sandbox is ignored: {}", error);
            }
        }
    }

    /*
     * The composition becomes the environment of the process: from here on
     * every reader sees it, whether it uses an entry point of the environment
     * API or walks the block of the process parameters itself.
     */
    InstallBlock();

    LOG_I("environment isolation loaded: {} variables, {} configured, {} kept from an earlier run, inherited {}",
          appbox::sandbox->env_table.Count(), configured, modifications, appbox::sandbox->bEnvironmentComposed);

    return STATUS_SUCCESS;
}

void appbox::environment::Isolation::Exit()
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    /*
     * The table and the state belong to the sandbox instance, which owns them
     * for the run. The block of the process was handed to the operating system
     * when it was installed, so it is not released here.
     */
    appbox::sandbox->env_table.Clear();
    appbox::sandbox->env_state.Clear();
}

bool appbox::environment::Isolation::IsEnabled()
{
    return appbox::sandbox != nullptr && appbox::sandbox->bIsolationMode;
}

bool appbox::environment::Isolation::Store(const std::wstring& name, const std::wstring& value)
{
    if (appbox::sandbox == nullptr)
    {
        return false;
    }

    /*
     * The name of a variable must carry a value and must not carry an equals
     * sign, because the equals sign separates the name from the value inside
     * the environment block of a process.
     */
    if (name.empty() || name.find(L'=') != std::wstring::npos)
    {
        return false;
    }

    appbox::sandbox->env_table.Set(name, value);

    if (appbox::sandbox->env_state.HasValue(name, value))
    {
        /* The state already carries the modification, so it is not sent again. */
        return true;
    }

    appbox::sandbox->env_state.Record(name, value);
    PersistState();
    return true;
}

bool appbox::environment::Isolation::Remove(const std::wstring& name)
{
    if (appbox::sandbox == nullptr)
    {
        return false;
    }

    if (name.empty() || name.find(L'=') != std::wstring::npos)
    {
        return false;
    }

    appbox::sandbox->env_table.Delete(name);

    if (appbox::sandbox->env_state.HasDeletion(name))
    {
        /* The state already carries the modification, so it is not sent again. */
        return true;
    }

    appbox::sandbox->env_state.RecordDeletion(name);
    PersistState();
    return true;
}

void appbox::environment::Isolation::AssignBlock(const wchar_t* block, std::size_t size_in_bytes)
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    /*
     * The block the application installed is the new environment of the process,
     * so it replaces the table of the sandbox. The state records the difference
     * between the block and the environment the sandbox composed, because that
     * difference is what the next run has to apply on top of its own layers: a
     * block which carries the environment of the process unchanged records
     * nothing, and a variable the block dropped is a removal.
     */
    appbox::environment::Table installed;
    installed.AssignBlock(block, size_in_bytes);

    for (const auto& variable : installed.Entries())
    {
        if (variable.name.empty() || variable.name.find(L'=') != std::wstring::npos)
        {
            /* The state of the sandbox refuses such a name, see Store(). */
            continue;
        }

        std::wstring composed;
        if (!appbox::sandbox->env_table.Get(variable.name, composed) || composed != variable.value)
        {
            appbox::sandbox->env_state.Record(variable.name, variable.value);
        }
    }

    for (const auto& variable : appbox::sandbox->env_table.Entries())
    {
        if (variable.name.empty() || variable.name.find(L'=') != std::wstring::npos)
        {
            continue;
        }

        if (!installed.Contains(variable.name))
        {
            appbox::sandbox->env_state.RecordDeletion(variable.name);
        }
    }

    appbox::sandbox->env_table.AssignBlock(block, size_in_bytes);

    PersistState();
}

bool appbox::environment::IsProcessEnvironment(PWSTR* environment)
{
    if (environment == nullptr)
    {
        /* A null block names the environment of the process. */
        return true;
    }

    /*
     * The address of the environment pointer of the process parameters names the
     * same environment: `RtlSetEnvironmentVar` compares the block it is given
     * against that pointer and takes the lock of the process block when the two
     * match, so the two spellings are one call.
     */
    return environment == appbox::GetProcessEnvironmentSlot();
}

void appbox::environment::Isolation::Clear()
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    appbox::sandbox->env_table.Clear();
    appbox::sandbox->env_state.Clear();
    PersistState();
}

FARPROC appbox::environment::ResolveEnvironmentProc(const char* name)
{
    FARPROC address = ::GetProcAddress(appbox::sys.h_kernelbase, name);
    if (address == nullptr)
    {
        address = ::GetProcAddress(appbox::sys.h_kernel32, name);
    }
    return address;
}

bool appbox::environment::AnsiToWide(const std::string& text, std::wstring& out)
{
    if (text.empty())
    {
        out.clear();
        return true;
    }

    const int size = ::MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0)
    {
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    return ::MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), size) == size;
}

std::string appbox::environment::BuildChildInjectData()
{
    if (appbox::sandbox == nullptr)
    {
        return {};
    }

    try
    {
        nlohmann::json config = nlohmann::json::parse(appbox::sandbox->inject_data);
        config["environment_is_composed"] = true;
        return config.dump();
    }
    catch (const std::exception& e)
    {
        /*
         * The child is started with the configuration of this process, which
         * composes its environment a second time: that is wrong for a variable
         * the merge mode joins, so the reason is logged.
         */
        LOG_W("the configuration of a child process cannot be adjusted: {}", e.what());
        return appbox::sandbox->inject_data;
    }
}

bool appbox::environment::ReadUnicodeStringText(const PUNICODE_STRING text, std::wstring& out)
{
    out.clear();

    if (text == nullptr || text->Buffer == nullptr || text->Length == 0)
    {
        return false;
    }

    if (text->Length > text->MaximumLength || (text->Length % sizeof(wchar_t)) != 0)
    {
        return false;
    }

    out.assign(text->Buffer, static_cast<std::size_t>(text->Length) / sizeof(wchar_t));
    return true;
}

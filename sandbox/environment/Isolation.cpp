#include "utils/WinAPI.h" /* Must be first include file */
#include <exception>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>
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
 * @brief Read the value the host holds for a variable.
 *
 * @param[in] host The environment of the host.
 * @param[in] name Name to look for, compared ignoring the case.
 * @param[out] value Value of the host.
 * @return true when the host holds the variable.
 */
bool HostValue(const std::vector<appbox::environment::Variable>& host, const std::wstring& name, std::wstring& value)
{
    for (const auto& variable : host)
    {
        if (appbox::environment::NamesEqual(variable.name, name))
        {
            value = variable.value;
            return true;
        }
    }
    return false;
}

/**
 * @brief Apply the variables of the environment isolation file.
 *
 * The values of the host are the ones the block of this process holds, so the
 * composition of a variable does not depend on the variables which were
 * composed before it.
 *
 * The value of a row may reference a known folder of the machine with
 * `%APPBOX:<NAME>%`; the reference is replaced before the value is composed
 * with the value of the host. A value the application stored at run time is
 * not expanded: it is part of the state and is applied as it is.
 *
 * @param[in] host The environment of the host.
 * @param[in] variables The variables of the isolation file.
 */
void ApplyConfiguration(const std::vector<appbox::environment::Variable>&           host,
                        const std::vector<appbox::environment::ConfiguredVariable>& variables)
{
    for (const auto& variable : variables)
    {
        std::wstring host_value;
        const bool   host_present = HostValue(host, variable.name, host_value);

        /*
         * The value the user entered may reference a known folder of this
         * machine with `%APPBOX:<NAME>%`. The reference is replaced before the
         * value is composed with the value of the host, so the merge joins the
         * expanded text.
         */
        const std::wstring value = appbox::ExpandVariables(variable.value, appbox::sandbox->variables);

        const appbox::environment_isolation::ComposedValue composed =
            appbox::environment_isolation::ComposeEnvironmentValue(host_present, host_value, value, variable.isolation,
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
 * @brief Send the state of the sandbox to the loader.
 *
 * The loader owns the state directory of the sandbox and writes the document
 * the sandbox sends over the RPC pipe. A failure is not fatal: the environment
 * of the running process is the table, which stays correct, and only the next
 * run would miss the modification.
 */
void PersistState()
{
    if (appbox::sandbox == nullptr || appbox::sandbox->client == nullptr)
    {
        return;
    }

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
            LOG_W("the loader did not keep the environment of the sandbox");
        }
    }
    catch (const std::exception& e)
    {
        LOG_W("the environment of the sandbox cannot be kept: {}", e.what());
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
        /* The composition reads the values of the host, so they are kept apart. */
        const std::vector<Variable> host = appbox::sandbox->env_table.Entries();

        const std::wstring& isolation_path = appbox::sandbox->wEnvironmentIsolationDOSPath;
        if (!isolation_path.empty())
        {
            std::string text;
            if (ReadTextFile(isolation_path, text))
            {
                std::vector<ConfiguredVariable> variables;
                std::string                     error;
                if (ParseIsolationDocument(text, variables, error))
                {
                    ApplyConfiguration(host, variables);
                    configured = variables.size();
                }
                else
                {
                    LOG_W("the environment isolation file is ignored: {}", error);
                }
            }
            else
            {
                LOG_D("the environment isolation file does not exist: {}", appbox::WideToUTF8(isolation_path));
            }
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
     * for the run. The hooks are detached before this runs, so no block the
     * table handed out is used any more.
     */
    appbox::sandbox->env_table.Clear();
    appbox::sandbox->env_state.Clear();
}

bool appbox::environment::Isolation::IsEnabled()
{
    return appbox::sandbox != nullptr && appbox::sandbox->bIsolationMode;
}

std::size_t appbox::environment::Isolation::Count()
{
    return appbox::sandbox == nullptr ? 0 : appbox::sandbox->env_table.Count();
}

bool appbox::environment::Isolation::Contains(const std::wstring& name)
{
    return appbox::sandbox != nullptr && appbox::sandbox->env_table.Contains(name);
}

bool appbox::environment::Isolation::Query(const std::wstring& name, std::wstring& value)
{
    return appbox::sandbox != nullptr && appbox::sandbox->env_table.Get(name, value);
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
    appbox::sandbox->env_state.RecordDeletion(name);
    PersistState();
    return true;
}

void appbox::environment::Isolation::AssignBlock(const wchar_t* block)
{
    if (appbox::sandbox == nullptr)
    {
        return;
    }

    /*
     * The application replaced the whole environment, so the state is replaced
     * as well: the block is the new environment and nothing of what the
     * application did before is part of it any more.
     */
    appbox::sandbox->env_table.AssignBlock(block);
    appbox::sandbox->env_state.Clear();

    for (const auto& variable : appbox::sandbox->env_table.Entries())
    {
        appbox::sandbox->env_state.Record(variable.name, variable.value);
    }

    PersistState();
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

wchar_t* appbox::environment::Isolation::CreateBlock()
{
    return appbox::sandbox == nullptr ? nullptr : appbox::sandbox->env_table.CreateBlock();
}

wchar_t* appbox::environment::Isolation::CreateOwnedBlock()
{
    return appbox::sandbox == nullptr ? nullptr : appbox::sandbox->env_table.CreateOwnedBlock();
}

bool appbox::environment::Isolation::ReleaseBlock(wchar_t* block)
{
    return appbox::sandbox == nullptr ? false : appbox::sandbox->env_table.ReleaseBlock(block);
}

char* appbox::environment::Isolation::CreateAnsiBlock()
{
    return appbox::sandbox == nullptr ? nullptr : appbox::sandbox->env_table.CreateAnsiBlock();
}

bool appbox::environment::Isolation::ReleaseAnsiBlock(char* block)
{
    return appbox::sandbox == nullptr ? false : appbox::sandbox->env_table.ReleaseAnsiBlock(block);
}

std::wstring appbox::environment::Isolation::Expand(const std::wstring& text)
{
    std::wstring result;
    result.reserve(text.size());

    std::size_t index = 0;
    while (index < text.size())
    {
        if (text[index] != L'%')
        {
            result.push_back(text[index]);
            ++index;
            continue;
        }

        const std::size_t end = text.find(L'%', index + 1);
        if (end == std::wstring::npos)
        {
            /* A lone percent sign is copied, like the operating system does. */
            result.push_back(text[index]);
            ++index;
            continue;
        }

        const std::wstring name = text.substr(index + 1, end - index - 1);

        std::wstring value;
        if (name.empty() || !Query(name, value))
        {
            /* A reference the environment does not hold keeps its spelling. */
            result.append(text, index, end - index + 1);
            index = end + 1;
            continue;
        }

        result.append(value);
        index = end + 1;
    }

    return result;
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

bool appbox::environment::WideToAnsi(const std::wstring& text, std::string& out)
{
    if (text.empty())
    {
        out.clear();
        return true;
    }

    const int size =
        ::WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0)
    {
        return false;
    }

    out.resize(static_cast<std::size_t>(size));
    return ::WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr,
                                 nullptr) == size;
}

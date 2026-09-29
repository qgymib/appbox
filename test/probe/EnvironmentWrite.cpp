#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include "EnvironmentWrite.hpp"
#include "WString.hpp"
#include <string>
#include <vector>

namespace
{

/**
 * @brief Read a variable with the wide entry point of the process environment.
 * @param[in] name Name of the variable.
 * @param[out] found Whether the environment holds the variable.
 * @return The value of the variable, empty when it is not held.
 */
std::string ReadWide(const std::wstring& name, bool& found)
{
    std::vector<wchar_t> buffer(32768);

    ::SetLastError(ERROR_SUCCESS);
    const DWORD length = ::GetEnvironmentVariableW(name.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));

    found = length != 0 || ::GetLastError() != ERROR_ENVVAR_NOT_FOUND;
    if (length == 0)
    {
        return std::string();
    }

    return appbox::WideToUTF8(std::wstring(buffer.data(), length));
}

} // namespace

static nlohmann::json ProbeEnvironmentWrite_Entry(const nlohmann::json& data)
{
    const auto req = data.get<appbox::test::ProtocolEnvironmentWrite::Req>();

    appbox::test::ProtocolEnvironmentWrite::Rsp rsp;

    /* <windows.h> defines min and max as macros, so the smaller size is picked here. */
    const std::size_t count = req.values.size() < req.names.size() ? req.values.size() : req.names.size();
    for (std::size_t index = 0; index < count; ++index)
    {
        const std::wstring name = appbox::UTF8ToWide(req.names[index]);
        const std::wstring value = appbox::UTF8ToWide(req.values[index]);
        rsp.stored.push_back(::SetEnvironmentVariableW(name.c_str(), value.c_str()) != FALSE);
    }

    for (const auto& name : req.remove)
    {
        const std::wstring wide_name = appbox::UTF8ToWide(name);
        rsp.removed.push_back(::SetEnvironmentVariableW(wide_name.c_str(), nullptr) != FALSE);
    }

    for (const auto& name : req.read)
    {
        bool found = false;
        rsp.values.push_back(ReadWide(appbox::UTF8ToWide(name), found));
        rsp.found.push_back(found);
    }

    return rsp;
}

appbox::test::Probe appbox::test::ProbeEnvironmentWrite("EnvironmentWrite", ProbeEnvironmentWrite_Entry);

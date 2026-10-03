/*
 * <winsock2.h> has to precede <windows.h>, which the headers below pull in
 * (<Shlobj.h>, the project headers): it defines _WINSOCKAPI_, so <windows.h>
 * skips the winsock 1.1 header, which cannot be included next to the winsock 2
 * header the RPC server reaches through asio.
 */
#include <winsock2.h>
#include <spdlog/spdlog.h>
#include <filesystem>
#include <fstream>
#include "sandbox/msg/Environment.hpp"
#include "WString.hpp"
#include "Launcher.hpp"
#include "__init__.hpp"

namespace
{

/**
 * @brief Write the environment state document of the sandbox.
 *
 * The document is written to a temporary file next to its destination and
 * moved over it afterwards, so a reader never sees a half written document.
 * The move replaces an existing file, which is what the second and every later
 * write of a run does.
 *
 * @param[in] path Destination of the state document.
 * @param[in] text UTF-8 text of the state document.
 * @param[out] error Error description on failure.
 * @return true on success.
 */
bool WriteEnvironmentState(const std::wstring& path, const std::string& text, std::string& error)
{
    error.clear();

    const std::filesystem::path target(path);
    const std::filesystem::path temporary = target.wstring() + L".tmp";

    std::error_code ec;
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec)
    {
        error = "failed to create '" + appbox::WideToUTF8(target.parent_path().wstring()) + "': " + ec.message();
        return false;
    }

    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream.is_open())
        {
            error = "failed to open '" + appbox::WideToUTF8(temporary.wstring()) + "'";
            return false;
        }

        stream.write(text.data(), static_cast<std::streamsize>(text.size()));
        stream.flush();
        if (!stream.good())
        {
            error = "failed to write '" + appbox::WideToUTF8(temporary.wstring()) + "'";
            return false;
        }
    }

    std::filesystem::rename(temporary, target, ec);
    if (ec)
    {
        error = "failed to move '" + appbox::WideToUTF8(temporary.wstring()) + "' over '" +
                appbox::WideToUTF8(target.wstring()) + "': " + ec.message();
        return false;
    }

    return true;
}

} // namespace

APPBOX_LAUNCHER_RPC_DEFINE(MsgEnvironment, id, param)
{
    const std::wstring path = LauncherApp().sandbox_paths.StateEnvironmentFile();

    std::string error;
    if (!WriteEnvironmentState(path, param.state, error))
    {
        SPDLOG_ERROR("failed to keep the environment of the sandbox: {}", error);
    }

    /*
     * The answer is sent after the document is on disk: the sandbox relies on
     * the answer to know that its modifications survive the end of the run.
     */
    LauncherApp().runtime->pipe_server->SendResponse(id, {});
}

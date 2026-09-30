#ifndef APPBOX_SANDBOX_MSG_ENVIRONMENT_HPP
#define APPBOX_SANDBOX_MSG_ENVIRONMENT_HPP

#include <nlohmann/json.hpp>
#include <string>

namespace appbox
{

/**
 * @brief Message which carries the environment state of the sandbox.
 *
 * The sandbox composes the environment of the packaged application and keeps
 * every modification the application makes to it. The document which describes
 * those modifications lives in the state directory of the sandbox, which the
 * launcher owns: the sandbox sends the document over the RPC pipe and the launcher
 * writes it to `data/environment/state.json`.
 *
 * The message carries the text of the document instead of its parts, so the
 * schema of the state file (`common/EnvironmentIsolation.hpp`) is known to the
 * sandbox alone and the launcher stays a writer of bytes.
 *
 * The response is sent after the file is on disk, so a sandbox which received
 * the answer knows that its state survives the end of the process.
 */
struct MsgEnvironment
{
    static constexpr const char* Method = "Environment";

    struct Req
    {
        /**
         * @brief UTF-8 text of the state document of the sandbox.
         */
        std::string state;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(Req, state)
    };

    struct Rsp
    {
        int _ = 0; /* Ignore */
        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(Rsp, _)
    };
};

} // namespace appbox

#endif // APPBOX_SANDBOX_MSG_ENVIRONMENT_HPP

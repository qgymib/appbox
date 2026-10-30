#include <spdlog/spdlog.h>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "SandboxId.hpp"
#include "utils/MultiInstancePipe.hpp"
#include "RunState.hpp"

appbox::test::RunStateClient::RunStateClient(const std::string& sandbox_id) : next_id_(1)
{
    /*
     * The control pipe of a sandbox is named by the identifier of the sandbox,
     * which every case derives from its own working directory: a client which
     * names no legal identifier cannot reach a run, which the failed connection
     * reports.
     */
    pipe_ = std::make_unique<RawControlPipe>(appbox::SandboxPipeName(sandbox_id));
}

appbox::test::RunStateClient::~RunStateClient() = default;

bool appbox::test::RunStateClient::IsOpen() const
{
    return pipe_ != nullptr && pipe_->IsOpen();
}

bool appbox::test::RunStateClient::Query(appbox::MsgRunState::Rsp& state, std::uint32_t timeout_ms)
{
    state = appbox::MsgRunState::Rsp{};

    if (!IsOpen())
    {
        return false;
    }

    appbox::MsgRunState::Req request;
    request.version = appbox::kRunStateVersion;

    nlohmann::json response;
    if (!pipe_->Call(next_id_++, appbox::MsgRunState::Method, nlohmann::json(request), response, timeout_ms))
    {
        SPDLOG_WARN("the state query of the sandbox was not answered: {}", pipe_->LastError());
        return false;
    }

    const auto it_result = response.find("result");
    if (it_result == response.end() || !it_result->is_object())
    {
        SPDLOG_WARN("the answer of the state query carries no state: {}", response.dump());
        return false;
    }

    try
    {
        state = it_result->get<appbox::MsgRunState::Rsp>();
    }
    catch (const nlohmann::json::exception& e)
    {
        SPDLOG_ERROR("the state of the run cannot be read: {}", e.what());
        return false;
    }

    /*
     * A refused query carries no state, so the zero of every member is no fact
     * of the run: a case which read it as one would pass for the wrong reason.
     */
    if (!state.error_code.empty())
    {
        SPDLOG_WARN("the state query of the sandbox was refused: {} ({})", state.error_code, state.error_message);
        return false;
    }

    return true;
}

std::uint32_t appbox::test::RunStateClient::LastError() const
{
    return pipe_ != nullptr ? pipe_->LastError() : 0;
}

bool appbox::test::QueryRunState(const std::string& sandbox_id, appbox::MsgRunState::Rsp& state,
                                 std::uint32_t timeout_ms)
{
    RunStateClient client(sandbox_id);
    if (!client.IsOpen())
    {
        SPDLOG_WARN("the control pipe of the sandbox '{}' cannot be opened", sandbox_id);
        return false;
    }

    return client.Query(state, timeout_ms);
}

std::vector<appbox::MsgRunState::Program> appbox::test::RequestPrograms(const appbox::MsgRunState::Rsp& state)
{
    std::vector<appbox::MsgRunState::Program> programs;

    for (const auto& program : state.programs)
    {
        if (!program.initial)
        {
            programs.push_back(program);
        }
    }

    return programs;
}

std::size_t appbox::test::RunningPrograms(const appbox::MsgRunState::Rsp& state)
{
    std::size_t running = 0;

    for (const auto& program : state.programs)
    {
        if (!program.exited && !program.terminated)
        {
            ++running;
        }
    }

    return running;
}

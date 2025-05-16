#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include "winapi.hpp"
#include "__init__.hpp"

struct SuperviseCtx
{
    std::wstring self_path;
};

static SuperviseCtx* G = nullptr;

void appbox::supervise::Init()
{
    G = new SuperviseCtx();
    G->self_path = appbox::GetExePath();
    spdlog::info(L"Loader path: {}", G->self_path);
}

void appbox::supervise::Exit()
{
    delete G;
    G = nullptr;
}


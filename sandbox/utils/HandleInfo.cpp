#include <mutex>
#include <map>
#include "HandleInfo.hpp"

typedef std::map<HANDLE, appbox::HandleInfo::Ptr> HandleInfoMap;

struct HandleInfoCtx
{
    HandleInfoMap info_map;
    std::mutex    info_map_mutex;
};

static HandleInfoCtx* s_handle_info_ctx = nullptr;

NTSTATUS appbox::HandleInfo::Init()
{
    if (s_handle_info_ctx != nullptr)
    {
        return STATUS_ALREADY_INITIALIZED;
    }

    s_handle_info_ctx = new HandleInfoCtx;
    return 0;
}

void appbox::HandleInfo::Exit()
{
    if (s_handle_info_ctx == nullptr)
    {
        return;
    }

    delete s_handle_info_ctx;
    s_handle_info_ctx = nullptr;
}

appbox::HandleInfo::Ptr appbox::HandleInfo::Create(HANDLE handle, Fn fn)
{
    auto info = std::make_shared<appbox::HandleInfo>();
    info->handle = handle;
    fn(info);

    {
        std::lock_guard<std::mutex> guard(s_handle_info_ctx->info_map_mutex);
        auto                        ret = s_handle_info_ctx->info_map.insert(HandleInfoMap::value_type(handle, info));
        if (!ret.second)
        {
            return nullptr;
        }
    }

    return info;
}

appbox::HandleInfo::Ptr appbox::HandleInfo::Find(HANDLE handle)
{
    std::lock_guard<std::mutex> guard(s_handle_info_ctx->info_map_mutex);
    auto                        it = s_handle_info_ctx->info_map.find(handle);
    if (it == s_handle_info_ctx->info_map.end())
    {
        return nullptr;
    }

    return it->second;
}

appbox::HandleInfo::Ptr appbox::HandleInfo::Pop(HANDLE handle)
{
    std::lock_guard<std::mutex> guard(s_handle_info_ctx->info_map_mutex);
    auto                        it = s_handle_info_ctx->info_map.find(handle);
    if (it == s_handle_info_ctx->info_map.end())
    {
        return nullptr;
    }

    auto info = it->second;
    s_handle_info_ctx->info_map.erase(it);

    return info;
}

#include <mutex>
#include <map>
#include "HandleInfo.hpp"

typedef std::map<HANDLE, appbox::HandleInfo::Ptr>         HandleInfoMap;
typedef std::map<uint64_t, appbox::HandleInfo::Meta::Ptr> HandleInfoMetaMap;

struct HandleInfoCtx
{
    HandleInfoMap info_map;
    std::mutex    info_map_mutex;
};

struct appbox::HandleInfo::Data
{
    HandleInfoMetaMap meta_map;
    std::mutex        meta_map_mutex;
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
    Ptr info(new appbox::HandleInfo);
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

appbox::HandleInfo::HandleInfo()
{
    data_ = new Data;
    handle = nullptr;
    ObjAttributes = 0;
}

appbox::HandleInfo::~HandleInfo()
{
    delete data_;
}

appbox::HandleInfo::Meta::Ptr appbox::HandleInfo::MetaFindOr(uint64_t key, Meta::CreateFn fn)
{
    std::lock_guard<std::mutex> guard(data_->meta_map_mutex);
    auto                        it = data_->meta_map.find(key);
    if (it != data_->meta_map.end())
    {
        return it->second;
    }

    if (fn == nullptr)
    {
        return nullptr;
    }

    auto meta = fn();
    if (meta.get() == nullptr)
    {
        return meta;
    }

    data_->meta_map.insert(HandleInfoMetaMap::value_type(key, meta));
    return meta;
}

void appbox::HandleInfo::MetaDrop(uint64_t key)
{
    std::lock_guard<std::mutex> guard(data_->meta_map_mutex);
    auto                        it = data_->meta_map.find(key);
    if (it != data_->meta_map.end())
    {
        data_->meta_map.erase(it);
    }
}

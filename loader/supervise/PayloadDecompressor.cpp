#include <wx/wx.h>
#include <spdlog/spdlog.h>
#include "utils/wstring.hpp"
#include "PayloadDecompressor.hpp"
#include "__init__.hpp"

PayloadDecompressor::PayloadDecompressor(HANDLE file, size_t size, const appbox::Meta& meta)
    : mStream(file, size)
{
    mMeta = meta;
}

bool PayloadDecompressor::WaitForCache(size_t size)
{
    while (mData.size() < size)
    {
        std::string cache = mStream.inflate();
        if (cache.empty())
        {
            return false;
        }
        mData.append(cache);
    }
    return true;
}

void PayloadDecompressor::Process()
{
    wxString sandboxLocation =  wxString::FromUTF8(mMeta.settings.sandboxLocation);
    spdlog::info(L"Sandbox location: {}", sandboxLocation.ToStdWstring());

    if (!wxMkdir(sandboxLocation))
    {
        wxString msg = wxString::Format(L"Create sandbox location failed: %s",
                                        sandboxLocation.c_str());
        throw std::runtime_error(msg.ToStdString(wxConvUTF8));
    }

    while (1)
    {
        switch (mCurrent.type)
        {
        case appbox::PAYLOAD_TYPE_NONE:
            if (!WaitForCache(sizeof(mCurrent)))
            {
                wxASSERT(mData.empty());
                return;
            }
            memcpy(&mCurrent, &mData[0], sizeof(mCurrent));
            mData.erase(0, sizeof(appbox::PayloadNode));
            break;

        case appbox::PAYLOAD_TYPE_FILESYSTEM:
            ProcessFilesystem();
            mCurrent.type = appbox::PAYLOAD_TYPE_NONE;
            break;

        default: {
            std::wstring msg =
                wxString::Format("Invalid payload type %d", mCurrent.type).ToStdWstring();
            throw std::runtime_error(appbox::wcstombs(msg.c_str()));
        }
        }
    }
}

void PayloadDecompressor::ProcessFilesystem()
{
    if (!WaitForCache(mCurrent.path_len))
    {
        throw std::runtime_error("Corrupted data");
    }

    std::string pathU8 = mData.substr(0, mCurrent.path_len);
    mData.erase(0, mCurrent.path_len);
    spdlog::info("Path: {}.", pathU8);

    if (!WaitForCache(mCurrent.payload_len))
    {
        throw std::runtime_error("Corrupted data");
    }
    std::string payload = mData.substr(0, mCurrent.payload_len);
    mData.erase(0, mCurrent.payload_len);

    // TODO: write to filesystem.
}

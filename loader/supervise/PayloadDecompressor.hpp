#ifndef APPBOX_LOADER_SUPERVISE_PAYLOAD_DECOMPRESSOR_HPP
#define APPBOX_LOADER_SUPERVISE_PAYLOAD_DECOMPRESSOR_HPP

#include <nlohmann/json.hpp>
#include "utils/meta.hpp"
#include "utils/zstream.hpp"

struct PayloadDecompressor
{
    PayloadDecompressor(HANDLE file, size_t size);
    bool WaitForCache(size_t size);
    void Process();
    void ProcessFilesystem();

    appbox::ZInflateStream mStream;  /* Inflate stream. */
    appbox::PayloadNode    mCurrent; /* Payload type current processing. */
    std::string            mData;    /* Payload data. */
    nlohmann::json         mMeta;    /* Metadata. */
};

#endif

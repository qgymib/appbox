#include "utils/WinAPI.h" /* Must be first include file */
#include "StreamName.hpp"
#include "Resolve.hpp"
#include "utils/CopyFileNt.hpp"
#include "CopyUp.hpp"

bool appbox::filesystem::CopyUpStreamFile(const std::wstring& view_path)
{
    const auto entry_path = EntryPathOfStream(view_path);
    if (entry_path == view_path)
    {
        /* The path names no stream, so no file has to travel with it. */
        return true;
    }

    /*
     * The view decides whether the file has to travel: the overlay keeps the
     * file it holds itself, and a file the view does not hold - a `Whiteout`
     * entry or an entry no layer carries - is left to the file system, which
     * creates it for the stream.
     */
    auto file = Resolve(entry_path);
    if (file->status != ResolveResult::Status::Exists || file->bInUpper)
    {
        return true;
    }

    return appbox::CopyFileNt(file->hPath[0].fPath, file->uPath);
}

bool appbox::filesystem::CopyUpEntry(const std::wstring& view_path, const ResolveResult& resolve)
{
    /*
     * The file which carries a stream travels first: a stream cannot exist
     * without its file, so the entry of the stream is not the entry the copy
     * of the stream starts from.
     */
    bool copied = CopyUpStreamFile(view_path);

    if (!resolve.bInUpper && !resolve.hPath.empty())
    {
        copied = appbox::CopyFileNt(resolve.hPath[0].fPath, resolve.uPath) && copied;
    }

    return copied;
}

#include "utils/WinAPI.h" /* Must be first include file */
#include "CreateDirectory.hpp"
#include "DirName.hpp"
#include "ReparsePoint.hpp"
#include "StreamName.hpp"
#include "Resolve.hpp"
#include "utils/CopyFileNt.hpp"
#include "utils/Log.hpp"
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
    const bool copied = CopyUpStreamFile(view_path);

    if (resolve.bInUpper || resolve.hPath.empty())
    {
        return copied;
    }

    const auto& hit = resolve.hPath[0];

    /*
     * The entry is a link, so the content it reaches is not what the overlay
     * has to carry: the object of the overlay carries the same reparse point,
     * which is what keeps the view from reporting a regular file where the
     * layer holds a link. A link which cannot be carried is reported as a
     * failure, because a copy of the object it names would shadow the link
     * with a different entry.
     */
    if ((hit.fInfo.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0)
    {
        CreateDirectories(DirName(resolve.uPath), resolve.uPathBaseSize);

        const NTSTATUS status = CopyReparsePointEntry(hit.fPath, resolve.uPath, hit.fInfo.FileAttributes);
        if (!NT_SUCCESS(status))
        {
            LOG_W(L"failed to copy the reparse point of {} into the overlay: {}", hit.fPath, status);
            return false;
        }

        return copied;
    }

    return appbox::CopyFileNt(hit.fPath, resolve.uPath) && copied;
}

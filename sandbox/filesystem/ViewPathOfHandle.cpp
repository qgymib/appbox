#include "utils/WinAPI.h" /* Must be first include file */
#include "utils/HandleInfo.hpp"
#include "utils/MappingAsDosNtPath.hpp"
#include "utils/QueryHandlePath.hpp"
#include "LayerPath.hpp"
#include "ViewPathOfHandle.hpp"

/**
 * @brief Whether the handle denotes an object of a local disk file system.
 *
 * The name of an object is asked for with `NtQueryObject`, which is known to
 * wait forever when it is called for a pipe handle which was opened for
 * synchronous input and output while a read is pending. The device of the
 * handle is therefore asked first: only an object of a disk file system can be
 * a path of the view, while every other object belongs to another isolation
 * domain and is forwarded, so the name of such an object is never asked for.
 *
 * The device is reported by the volume of the handle, which the call below
 * answers without waiting for the device, which is why it is used here instead
 * of the name.
 *
 * @param[in] handle Handle to test.
 * @return true when the handle denotes an object of a disk file system.
 */
static bool IsDiskHandle(HANDLE handle)
{
    return GetFileType(handle) == FILE_TYPE_DISK;
}

appbox::filesystem::HandlePathStatus appbox::filesystem::ViewPathOfHandle(HANDLE handle, std::wstring& viewPath)
{
    viewPath.clear();

    /* The record of an open the sandbox performed is authoritative. */
    auto info = appbox::HandleInfo::Find(handle);
    if (info.get() != nullptr && !info->viewPath.empty())
    {
        viewPath = info->viewPath;
        return HandlePathStatus::View;
    }

    if (!IsDiskHandle(handle))
    {
        return HandlePathStatus::Foreign;
    }

    std::wstring nativePath;
    if (!NT_SUCCESS(appbox::QueryHandlePath(handle, nativePath)) || nativePath.empty())
    {
        return HandlePathStatus::Unnamed;
    }

    /*
     * The name of the object is a device path of the file system. UNC paths,
     * named pipes, mailslots, network volumes and volumes which no drive letter
     * names cannot be expressed as a path of the view: they belong to another
     * isolation domain and are forwarded unchanged.
     */
    std::wstring dosPath;
    if (!appbox::MappingAsDosNtPath(nativePath, dosPath))
    {
        return HandlePathStatus::Foreign;
    }

    /*
     * A path which belongs to a layer is translated back into the path of the
     * view, and a path which belongs to the host filesystem is the path of the
     * view already.
     */
    std::wstring rebased;
    if (RebaseLayerPathToView(dosPath, rebased))
    {
        viewPath = rebased;
        return HandlePathStatus::View;
    }

    viewPath = dosPath;
    return HandlePathStatus::View;
}

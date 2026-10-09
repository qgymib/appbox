#ifndef APPBOX_SANDBOX_FILESYSTEM_VIEWPATHOFHANDLE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_VIEWPATHOFHANDLE_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox
{
namespace filesystem
{

/**
 * @brief What the view knows about the path of the object a handle denotes.
 */
enum class HandlePathStatus
{
    View,    /* The handle denotes a path of the view, the path is reported. */
    Foreign, /* The object belongs to another isolation domain: forward. */
    Unnamed, /* The object is a local object the view cannot name: refuse. */
};

/**
 * @brief Query the path of the view which a handle denotes.
 *
 * The information the sandbox recorded while it opened the handle is
 * authoritative, because a handle may denote the copy the upper layer holds
 * while the application knows the path of the view. A handle the sandbox did
 * not open, which is the handle a process inherited or duplicated, carries no
 * such record: its path is asked of the object manager and translated back
 * into the path of the view, so a call which acts on the handle of an
 * application behaves like the call of a path.
 *
 * The device of the handle is asked before its name, because asking the name
 * of a pipe handle whose reads are pending can wait forever, and because an
 * object which no disk file system holds can never be a path of the view.
 *
 * @param[in] handle Handle to query.
 * @param[out] viewPath Path of the view the handle denotes, set when the
 *                      status is `View`.
 * @return `View` when the handle denotes a path of the view, `Foreign` when
 *         the object belongs to another isolation domain and has to be
 *         forwarded, and `Unnamed` when the object is local but the view
 *         cannot name it, in which case the view refuses the call instead of
 *         answering it from the layer the handle was opened with.
 */
HandlePathStatus ViewPathOfHandle(HANDLE handle, std::wstring& viewPath);

} // namespace filesystem
} // namespace appbox

#endif // APPBOX_SANDBOX_FILESYSTEM_VIEWPATHOFHANDLE_HPP

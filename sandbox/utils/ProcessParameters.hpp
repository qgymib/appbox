#ifndef APPBOX_SANDBOX_UTILS_PROCESS_PARAMETERS_HPP
#define APPBOX_SANDBOX_UTILS_PROCESS_PARAMETERS_HPP

#include "utils/WinAPI.h"
#include <string>

namespace appbox::process_parameters
{

/**
 * @brief The image name entry of the attribute list of a creation.
 *
 * The kernel takes the image of a process out of the attribute list of the
 * call: the entry of the image name carries the buffer of the path and its
 * length in bytes, which is the value a sandbox replaces while the call runs.
 * The entry belongs to the caller, so the value it held is restored when the
 * object leaves, and the buffer the sandbox installs lives in the object.
 *
 * The entry is looked up by the number of the attribute and not by its
 * position, because the order of the list belongs to the caller.
 */
class ImageNameAttribute
{
public:
    /**
     * @brief Look up the entry of the image name.
     * @param[in] attributes The attribute list of the call, may be null.
     */
    explicit ImageNameAttribute(PPS_ATTRIBUTE_LIST attributes);

    ~ImageNameAttribute();

    ImageNameAttribute(const ImageNameAttribute&) = delete;
    ImageNameAttribute& operator=(const ImageNameAttribute&) = delete;

    /**
     * @brief Whether the list carries an entry of the image name.
     * @return true when an entry was found.
     */
    bool IsValid() const;

    /**
     * @brief Read the image name the caller named.
     * @param[out] name The path of the image.
     * @return true when the entry carries a usable text.
     */
    bool GetName(std::wstring& name) const;

    /**
     * @brief Install a path for the call which is forwarded.
     *
     * The value is installed until the object leaves, so a caller of the hook
     * which forwards the call sees its own value again afterwards.
     *
     * @param[in] path Path of the image, in the form the kernel expects.
     */
    void Set(const std::wstring& path);

private:
    /** The entry of the image name, null when the list carries none. */
    PS_ATTRIBUTE* entry_;

    /** The value and the length of the entry before the sandbox changed them. */
    ULONG_PTR saved_value_;
    SIZE_T    saved_size_;
    bool      bSaved_;

    /** The buffer the sandbox installs, alive while the object is. */
    std::wstring text_;
};

/**
 * @brief The values of the process parameters a child of the view has to see.
 *
 * The parameters of a creation belong to the caller, and the kernel reads them
 * while the call runs, so the sandbox installs the values of the view for the
 * duration of the call and restores the values of the caller afterwards. The
 * buffers the sandbox installs live in this object, which is what keeps the
 * strings of the view alive while the call reads them.
 *
 * The working directory is the one member which needs no value of its own: a
 * child inherits the directory of its parent, and the directory of a sandboxed
 * process names a layer once the view is mounted. The sandbox therefore only
 * maps such a path back into the view and leaves every other value alone.
 */
class ViewParameters
{
public:
    /**
     * @brief Take the parameters of a creation.
     * @param[in] parameters The process parameters of the call, may be null.
     */
    explicit ViewParameters(PRTL_USER_PROCESS_PARAMETERS parameters);

    ~ViewParameters();

    ViewParameters(const ViewParameters&) = delete;
    ViewParameters& operator=(const ViewParameters&) = delete;

    /**
     * @brief Install the path of the image the child runs.
     *
     * The value is a path of the process and not of the object namespace, so
     * the caller passes the DOS form of the path.
     *
     * @param[in] path Path of the image, empty to keep the value of the caller.
     */
    void SetImagePath(const std::wstring& path);

    /**
     * @brief Install the working directory of the child.
     * @param[in] path Directory in the DOS form, empty to keep the value.
     */
    void SetCurrentDirectory(const std::wstring& path);

private:
    /**
     * @brief The working directory of the parameters.
     * @return The record of the directory, null when the parameters are null.
     */
    UNICODE_STRING* CurrentDirectory() const;

    /** The parameters of the call, null when the caller passed none. */
    PRTL_USER_PROCESS_PARAMETERS parameters_;

    /** The values of the caller, restored when the object leaves. */
    UNICODE_STRING saved_image_;
    UNICODE_STRING saved_directory_;
    bool           bImageSet_;
    bool           bDirectorySet_;

    /** The buffers the sandbox installs, alive while the object is. */
    std::wstring image_;
    std::wstring directory_;
};

/**
 * @brief Convert a path of the object namespace into the form a process
 *        parameter carries.
 *
 * @param[in] nt_path Path which may carry the `\??\` namespace.
 * @return The path without the namespace, which is the form of the image path
 *         and of the working directory of a process.
 */
std::wstring ToDosPath(const std::wstring& nt_path);

/**
 * @brief Read the working directory the parameters carry.
 *
 * @param[in] parameters The process parameters of a call, may be null.
 * @return The directory, empty when the parameters carry none.
 */
std::wstring GetCurrentDirectory(PRTL_USER_PROCESS_PARAMETERS parameters);

} // namespace appbox::process_parameters

#endif // APPBOX_SANDBOX_UTILS_PROCESS_PARAMETERS_HPP

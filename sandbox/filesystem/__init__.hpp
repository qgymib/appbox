#ifndef APPBOX_SANDBOX_FILESYSTEM_INIT_HPP
#define APPBOX_SANDBOX_FILESYSTEM_INIT_HPP

#include <string>
#include <vector>
#include <memory>

namespace appbox
{

struct OverlayFS
{
    /**
     * @brief NT path component vector.
     */
    typedef std::vector<std::wstring> ComponentVec;

    struct Data
    {
        typedef std::shared_ptr<Data> Ptr;

        Data();
        Data(const Data& orig);
        Data(Data&& orig) = delete;
        Data& operator=(const Data& orig) = delete;
        Data& operator=(Data&& orig) = delete;

        std::wstring root;               /* Root namespace */
        ComponentVec components;         /* NT path components. */
        bool         has_trailing_slash; /* Has trailing slash. */
        std::wstring ads;                /* Alternate data stream name. */
    };

    /**
     * @brief Create NT path object.
     * @param[in] path Virtual NT path.
     */
    OverlayFS() = default;
    OverlayFS(const std::wstring& path);
    OverlayFS(const OverlayFS& orig);
    OverlayFS(OverlayFS&& orig) noexcept;
    OverlayFS& operator=(const OverlayFS& orig);
    OverlayFS& operator=(OverlayFS&& orig);

    /**
     * @brief Check if NT path is valid.
     * @return true if valid, otherwise false.
     */
    bool IsValid() const;

    /**
     * @brief Get parent NT path.
     * @return NT path.
     */
    OverlayFS Parent() const;

    /**
     * @brief Append directory name to NT path.
     * @param[in] name Directory name.
     * @return NT path.
     */
    OverlayFS operator/(const std::wstring& name) const;

    /**
     * @brief Get path in base filesystem.
     * @param[in] base_path Base filesystem path, no trailing slash. If empty, return virtual fs path.
     * @return NT path in base filesystem.
     * @{
     */
    std::wstring RebasePath(const std::wstring& base_path) const;
    std::wstring RebasePath(const std::string& base_path) const;
    /**
     * @}
     */

    /**
     * @brief Get path in virtual filesystem.
     * @param[out] path Path string.
     * @param[in] with_trailing_slash Whether to append trailing slash if it is a directory.
     * @return Result.
     */
    std::wstring Path() const;

    /**
     * @brief Resolve path.
     * @param[out] path Path string.
     * @return Status.
     */
    bool Resolve(std::wstring& path) const;

    /**
     * @brief Private data.
     */
    Data::Ptr data_;
};

} // namespace appbox

#endif

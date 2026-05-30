#ifndef APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP
#define APPBOX_SANDBOX_FILESYSTEM_RESOLVE_HPP

#include "utils/WinAPI.h"
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace appbox::filesystem
{

struct ResolveResult
{
    enum class Status
    {
        Exists,           /* File exists. */
        NotFound,         /* File not exists. */
        HiddenByWhiteout, /* File not found because of whiteout. */
        BlockedByOpaque,  /* File not found because of opaque. */
    };

    struct Path
    {
        /**
         * @brief NT path
         */
        std::wstring fPath;

        /**
         * @brief Path information
         */
        FILE_BASIC_INFORMATION fInfo = {};
    };

    /**
     * @brief Resolve status.
     */
    Status status = Status::NotFound;

    /**
     * @brief Lookup attributes.
     */
    ULONG NameAttributes = OBJ_CASE_INSENSITIVE;

    /**
     * @brief True if parent path exists.
     */
    bool bParentExist = false;

    /**
     * @brief Actual file path in upper filesystem path.
     * @note The file may not exist.
     */
    std::wstring uPath;

    /**
     * @brief The size of base upper filesystem path that cannot be changed.
     * @note Calculated with std::wstring.
     */
    size_t uPathBaseSize = 0;

    /**
     * @brief True if file is exists in upper filesystem.
     */
    bool bInUpper = false;

    /**
     * @brief Actual file path in upper / lower / host filesystem.
     * @note The file always exists if status == Exists.
     */
    std::vector<Path> hPath;

    /**
     * @brief Whiteout file path in host filesystem.
     */
    std::wstring whiteoutPath;

    /**
     * @brief Opaque file path in host filesystem.
     */
    std::wstring opaquePath;

    /**
     * @brief True if whiteout file is exists in upper filesystem.
     */
    bool bWhiteoutInUpper = false;

    /**
     * @brief True if opaque file is exists in upper filesystem.
     */
    bool bOpaqueInUpper = false;
};
void to_json(nlohmann::json& j, const ResolveResult& r);

struct ResolveOption
{
    /**
     * @brief Stop search when first file is found.
     */
    bool bStopOnFirstFound = true;

    /**
     * @brief Lookup attributes.
     */
    ULONG NameAttributes = OBJ_CASE_INSENSITIVE;
};

/**
 * @brief Resolve virtual path to host path.
 * @param[in] vPath Virtual path in mapped view.
 * @return Resolve result.
 */
ResolveResult Resolve(const std::wstring& vPath, const ResolveOption& option = {});

} // namespace appbox::filesystem

#endif

#ifndef APPBOX_COMMON_ISOLATION_DOCUMENT_HPP
#define APPBOX_COMMON_ISOLATION_DOCUMENT_HPP

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>

namespace appbox
{

/**
 * @brief Error of an isolation document which does not fit its schema.
 *
 * The error is thrown by the `from_json()` of an isolation document and is
 * caught by the reader of the file, which reports the text of the error to its
 * caller. The document types of the four isolation domains live in `common/`,
 * because the packer writes the files and the sandbox reads them, so the two
 * sides share one description of what a document has to look like.
 */
class IsolationDocumentError : public std::runtime_error
{
public:
    /**
     * @brief Create the error of a document which does not fit its schema.
     * @param[in] message Description of the member which was refused.
     */
    explicit IsolationDocumentError(const std::string& message) : std::runtime_error(message)
    {
    }
};

/**
 * @brief Helpers shared by the `from_json()` of the isolation documents.
 *
 * Every document reads its members with the helpers below, so the four domains
 * refuse a broken member with the same wording and no domain hand parses a JSON
 * object of its own. The helpers never touch the Windows API and never include
 * `common/WString.hpp`: the isolation headers are included by the socket code
 * of the sandbox, which has to see `<winsock2.h>` before `<windows.h>`, so the
 * shared headers stay free of `<windows.h>`. The texts of a document are
 * therefore handled as UTF-8 like the JSON document which carries them, and the
 * conversion to wide text happens in the code which builds or applies a
 * document.
 */
namespace isolation_document
{

/**
 * @brief Reject a document which does not fit its schema.
 * @param[in] message Description of the member which was refused.
 * @throw IsolationDocumentError Always.
 */
[[noreturn]] inline void Throw(const std::string& message)
{
    throw IsolationDocumentError(message);
}

/**
 * @brief Reject a value which is not a JSON object.
 * @param[in] json The value to check.
 * @param[in] holder Description of the value, used by the error text, for
 *                   example `"a filesystem isolation file entry"`.
 * @throw IsolationDocumentError The value is not an object.
 */
inline void RequireObject(const nlohmann::json& json, const std::string& holder)
{
    if (!json.is_object())
    {
        Throw(holder + " is not a JSON object");
    }
}

/**
 * @brief Find the member of a JSON object.
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @return The member, null when the object does not hold it.
 */
inline const nlohmann::json* FindMember(const nlohmann::json& json, const char* key)
{
    const auto member = json.find(key);
    return member == json.end() ? nullptr : &(*member);
}

/**
 * @brief Read a required member which holds a text.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @param[in] holder Description of the object, used by the error text.
 * @return The text of the member.
 * @throw IsolationDocumentError The member is missing or is not a text.
 */
inline std::string RequiredText(const nlohmann::json& json, const char* key, const std::string& holder)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr)
    {
        Throw(holder + " has no '" + key + "' member");
    }
    if (!member->is_string())
    {
        Throw("the '" + std::string(key) + "' member of " + holder + " is not a string");
    }

    return member->get<std::string>();
}

/**
 * @brief Read a required member which holds a flag.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @param[in] holder Description of the object, used by the error text.
 * @return The value of the member.
 * @throw IsolationDocumentError The member is missing or is not a flag.
 */
inline bool RequiredFlag(const nlohmann::json& json, const char* key, const std::string& holder)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr)
    {
        Throw(holder + " has no '" + key + "' member");
    }
    if (!member->is_boolean())
    {
        Throw("the '" + std::string(key) + "' member of " + holder + " is not a boolean");
    }

    return member->get<bool>();
}

/**
 * @brief Read a required member which holds a whole number.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @param[in] holder Description of the object, used by the error text.
 * @return The number of the member.
 * @throw IsolationDocumentError The member is missing or is not a whole number.
 */
inline int RequiredInt(const nlohmann::json& json, const char* key, const std::string& holder)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr)
    {
        Throw(holder + " has no '" + key + "' member");
    }
    if (!member->is_number_integer())
    {
        Throw("the '" + std::string(key) + "' member of " + holder + " is not a whole number");
    }

    return member->get<int>();
}

/**
 * @brief Get the member which holds a list of entries.
 *
 * A member which the object does not hold is not an error: a hand written
 * document may list no entry at all, which describes a file without a mode.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @param[in] holder Description of the object, used by the error text.
 * @return The list of the member, null when the object does not hold it.
 * @throw IsolationDocumentError The member is present but is not a list.
 */
inline const nlohmann::json* OptionalArray(const nlohmann::json& json, const char* key, const std::string& holder)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr)
    {
        return nullptr;
    }
    if (!member->is_array())
    {
        Throw("the '" + std::string(key) + "' member of " + holder + " is not a list");
    }

    return member;
}

/**
 * @brief Read a member which holds a text, empty when it carries none.
 *
 * A member which is missing or which holds a value of another type is read as
 * an empty text instead of an error. The network proxy of a session uses the
 * lenient reading, because a proxy which cannot be used describes a session
 * without a proxy and never fails the whole document: the entries of the
 * document still redirect the names of the application.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @return The text of the member, empty when it is absent or not a text.
 */
inline std::string LenientText(const nlohmann::json& json, const char* key)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr || !member->is_string())
    {
        return {};
    }

    return member->get<std::string>();
}

/**
 * @brief Read a member which holds a flag, false when it carries none.
 *
 * The lenient reading is the one of LenientText() and is used by the network
 * proxy for the same reason.
 *
 * @param[in] json The object to read.
 * @param[in] key Name of the member.
 * @return The value of the member, false when it is absent or not a flag.
 */
inline bool LenientFlag(const nlohmann::json& json, const char* key)
{
    const auto* member = FindMember(json, key);
    if (member == nullptr || !member->is_boolean())
    {
        return false;
    }

    return member->get<bool>();
}

} // namespace isolation_document

} // namespace appbox

#endif // APPBOX_COMMON_ISOLATION_DOCUMENT_HPP

#include "ProjectType.hpp"
#include <string>

namespace
{

/**
 * @brief One entry of the `Project Type` box.
 *
 * The table is the single place which spells a project type: the token of the
 * project file, the label of the box and the order of the entries are derived
 * from it, so the three of them cannot drift apart.
 */
struct ProjectTypeEntry
{
    appbox::ProjectType type;  ///< The type of the entry.
    const char*         token; ///< Token stored by the project file.
    const wchar_t*      label; ///< Label shown by the `Project Type` box.
};

/** The entries of the `Project Type` box, in the order of the box. */
constexpr ProjectTypeEntry kProjectTypes[] = {
    { appbox::ProjectType::Standalone, "standalone", L"Standalone (ZIP)" },
    { appbox::ProjectType::Patch,      "patch",      L"Patch (ZIP)"      },
};

/** Number of entries of the table above. */
constexpr std::size_t kProjectTypeCount = sizeof(kProjectTypes) / sizeof(kProjectTypes[0]);

/**
 * @brief Lower the ASCII letters of a token.
 *
 * The tokens are ASCII, so the fold is done by hand instead of through a
 * locale aware conversion.
 *
 * @param[in] token The token to fold.
 * @return The token with every upper case letter lowered.
 */
std::string ToLowerToken(std::string_view token)
{
    std::string lowered;
    lowered.reserve(token.size());
    for (const char character : token)
    {
        lowered.push_back(character >= 'A' && character <= 'Z' ? static_cast<char>(character - 'A' + 'a') : character);
    }
    return lowered;
}

} // namespace

namespace appbox
{

const char* ProjectTypeToken(ProjectType type)
{
    for (const auto& entry : kProjectTypes)
    {
        if (entry.type == type)
        {
            return entry.token;
        }
    }

    return kProjectTypes[0].token;
}

bool ParseProjectTypeToken(std::string_view token, ProjectType& type)
{
    const auto lowered = ToLowerToken(token);
    for (const auto& entry : kProjectTypes)
    {
        if (lowered == entry.token)
        {
            type = entry.type;
            return true;
        }
    }

    return false;
}

const wchar_t* ProjectTypeDisplayName(ProjectType type)
{
    for (const auto& entry : kProjectTypes)
    {
        if (entry.type == type)
        {
            return entry.label;
        }
    }

    return kProjectTypes[0].label;
}

std::size_t ProjectTypeCount()
{
    return kProjectTypeCount;
}

ProjectType ProjectTypeAt(std::size_t index)
{
    if (index >= kProjectTypeCount)
    {
        return kProjectTypes[0].type;
    }

    return kProjectTypes[index].type;
}

std::size_t ProjectTypeIndexOf(ProjectType type)
{
    for (std::size_t index = 0; index < kProjectTypeCount; ++index)
    {
        if (kProjectTypes[index].type == type)
        {
            return index;
        }
    }

    return 0;
}

} // namespace appbox

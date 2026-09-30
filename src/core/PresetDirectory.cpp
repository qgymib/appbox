#include "PresetDirectory.hpp"
#include "utils/KnownFolder.hpp"
#include <spdlog/spdlog.h>

namespace
{

/**
 * @brief Static definition of one preset directory.
 */
struct PresetDefinition
{
    const char*    id;           ///< Stable identifier.
    const wchar_t* display_name; ///< Tree label.
    const wchar_t* layer_key;    ///< Lower layer key token.
    const char*    parent_id;    ///< Holding preset, `nullptr` at top level.
};

/*
 * A preset which hangs below another one must follow it in the table, so the
 * resolution below can tell whether the parent is offered as well.
 */
const PresetDefinition s_preset_definitions[] = {
    { "program_files", L"Program Files",          L"#ProgramFiles#", nullptr        },
    { "user_profile",  L"Current User Directory", L"#USERPROFILE#",  nullptr        },
    { "documents",     L"Documents",              L"#Documents#",    "user_profile" },
    { "desktop",       L"Desktop",                L"#Desktop#",      "user_profile" },
    { "windows",       L"Windows",                L"#Windows#",      nullptr        },
    { "system32",      L"System32",               L"#System32#",     "windows"      },
    { "fonts",         L"Fonts",                  L"#Fonts#",        "windows"      },
};

/**
 * @brief Whether a list of resolved presets already holds an identifier.
 * @param[in] presets Resolved preset directories.
 * @param[in] id Identifier to look for.
 * @return true when the identifier is present.
 */
bool HoldsPreset(const std::vector<appbox::PresetDirectory>& presets, const char* id)
{
    for (const auto& preset : presets)
    {
        if (preset.id == id)
        {
            return true;
        }
    }
    return false;
}

} // namespace

namespace appbox
{

const std::vector<PresetDirectory>& PresetDirectories()
{
    static const std::vector<PresetDirectory> presets = []() {
        std::vector<PresetDirectory> result;
        for (const auto& definition : s_preset_definitions)
        {
            /*
             * A nested preset is only offered together with the preset which
             * holds it: without its parent the tree would have no item to hang
             * the preset on.
             */
            if (definition.parent_id != nullptr && !HoldsPreset(result, definition.parent_id))
            {
                SPDLOG_WARN("failed to resolve the parent of the preset directory: {}", definition.id);
                continue;
            }

            std::wstring real_path;
            if (!SearchFolderID(definition.layer_key, real_path))
            {
                SPDLOG_WARN(L"failed to resolve the preset directory: {}", definition.layer_key);
                continue;
            }

            PresetDirectory preset;
            preset.id = definition.id;
            preset.parent_id = definition.parent_id != nullptr ? definition.parent_id : "";
            preset.display_name = definition.display_name;
            preset.layer_key = definition.layer_key;
            preset.real_path = real_path;
            result.push_back(std::move(preset));
        }
        return result;
    }();
    return presets;
}

std::vector<PresetDirectory> ChildPresets(const std::string& parent_id)
{
    std::vector<PresetDirectory> result;
    for (const auto& preset : PresetDirectories())
    {
        if (preset.parent_id == parent_id)
        {
            result.push_back(preset);
        }
    }
    return result;
}

bool FindPresetDirectory(const std::string& id, PresetDirectory& out)
{
    for (const auto& preset : PresetDirectories())
    {
        if (preset.id == id)
        {
            out = preset;
            return true;
        }
    }
    return false;
}

} // namespace appbox

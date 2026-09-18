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
};

const PresetDefinition s_preset_definitions[] = {
    { "program_files", L"Program Files",         L"%ProgramFiles%" },
    { "user_profile",  L"Current User Directory", L"%USERPROFILE%"  },
};

} // namespace

namespace appbox
{

const std::vector<PresetDirectory>& PresetDirectories()
{
    static const std::vector<PresetDirectory> presets = []() {
        std::vector<PresetDirectory> result;
        for (const auto& definition : s_preset_definitions)
        {
            std::wstring real_path;
            if (!SearchFolderID(definition.layer_key, real_path))
            {
                SPDLOG_WARN(L"failed to resolve the preset directory: {}", definition.layer_key);
                continue;
            }

            PresetDirectory preset;
            preset.id = definition.id;
            preset.display_name = definition.display_name;
            preset.layer_key = definition.layer_key;
            preset.real_path = real_path;
            result.push_back(std::move(preset));
        }
        return result;
    }();
    return presets;
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

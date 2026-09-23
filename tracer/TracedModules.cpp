#include "tracer/TracedModules.hpp"
#include <windows.h>
#include <algorithm>
#include <stdexcept>
#include <vector>

namespace appbox::tracer
{
namespace
{

/** Machine type of a 32 bit image (IMAGE_FILE_MACHINE_I386). */
constexpr std::uint16_t kMachineI386 = 0x014C;

/**
 * @brief Read a directory from the Windows API.
 *
 * @param[in] wow64 Whether the WOW64 directory is requested.
 * @return The directory, or an empty path when it is not available.
 */
std::filesystem::path QuerySystemDirectory(bool wow64)
{
    std::vector<wchar_t> buffer(MAX_PATH, L'\0');
    const UINT length = wow64 ? ::GetSystemWow64DirectoryW(buffer.data(), static_cast<UINT>(buffer.size()))
                              : ::GetSystemDirectoryW(buffer.data(), static_cast<UINT>(buffer.size()));
    if (length == 0 || length >= buffer.size())
    {
        return {};
    }

    return std::filesystem::path(std::wstring(buffer.data(), length));
}

} // namespace

std::vector<std::wstring> TracedModuleNames()
{
    return {L"ntdll", L"kernel32", L"kernelbase"};
}

std::wstring ModuleNameFromImagePath(const std::wstring& image_path)
{
    std::wstring name = std::filesystem::path(image_path).filename().wstring();
    if (name.empty())
    {
        return {};
    }

    const std::size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0)
    {
        name.erase(dot);
    }

    std::transform(name.begin(), name.end(), name.begin(), [](wchar_t character) {
        return character >= L'A' && character <= L'Z'
                   ? static_cast<wchar_t>(character - L'A' + L'a')
                   : character;
    });

    return name;
}

std::filesystem::path SystemDirectoryForMachine(std::uint16_t machine)
{
    const std::filesystem::path system_directory = QuerySystemDirectory(false);
    if (machine != kMachineI386)
    {
        return system_directory;
    }

    const std::filesystem::path wow64_directory = QuerySystemDirectory(true);
    return wow64_directory.empty() ? system_directory : wow64_directory;
}

ModuleImages LoadTracedModules(const std::filesystem::path& directory)
{
    std::map<std::wstring, std::filesystem::path> paths;
    for (const auto& name : TracedModuleNames())
    {
        paths[name] = directory / (name + L".dll");
    }

    return LoadTracedModules(paths);
}

ModuleImages LoadTracedModules(const std::map<std::wstring, std::filesystem::path>& paths)
{
    ModuleImages modules;
    for (const auto& path : paths)
    {
        try
        {
            TracedModule module;
            module.path = path.second;
            module.image = PeImage::FromFile(path.second);
            modules.emplace(path.first, std::move(module));
        }
        catch (const std::runtime_error&)
        {
            /* A module which can not be read simply stays out of the set. */
        }
    }

    return modules;
}

} // namespace appbox::tracer

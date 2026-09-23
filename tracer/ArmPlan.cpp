#include "tracer/ArmPlan.hpp"
#include "tracer/ScopePatterns.hpp"
#include "WString.hpp"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace appbox::tracer
{
namespace
{

/** Maximum length of a forwarder chain which is followed. */
constexpr int kMaxForwarderDepth = 8;

/**
 * @brief Report whether a name is safe to pass to the debugger.
 *
 * Export names are C identifiers, so anything else never occurs in practice.
 * The check keeps a malformed image from injecting extra debugger commands
 * through a breakpoint command string.
 *
 * @param[in] name Export name to test.
 * @return Whether every character of the name is expected.
 */
bool IsSafeExportName(const std::wstring& name)
{
    for (const wchar_t character : name)
    {
        const bool is_letter = (character >= L'a' && character <= L'z') ||
                               (character >= L'A' && character <= L'Z');
        const bool is_digit = (character >= L'0' && character <= L'9');
        const bool is_known = character == L'_' || character == L'@' || character == L'$' ||
                              character == L'?' || character == L'.';
        if (!is_letter && !is_digit && !is_known)
        {
            return false;
        }
    }

    return !name.empty();
}

/**
 * @brief Report whether an exported name is part of the requested categories.
 *
 * @param[in] name Export name to test.
 * @param[in] categories Categories to include.
 * @return Whether the name belongs to at least one of them.
 */
bool IsInScope(const std::wstring& name, const std::vector<Category>& categories)
{
    const std::vector<Category> found = ClassifyExport(name);
    for (const auto& category : categories)
    {
        if (std::find(found.begin(), found.end(), category) != found.end())
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Resolve an export name to the address of its implementation.
 *
 * The resolver follows forwarder chains. A module which is not part of the
 * traced set is parsed on demand from the module directory, because a forward
 * can pass through an API set DLL (`kernel32!AddDllDirectory` forwards to
 * `api-ms-win-core-libraryloader-l1-1-0.AddDllDirectory`).
 */
class ImplementationResolver
{
public:
    /**
     * @brief Constructor.
     *
     * @param[in] modules Parsed images of the traced modules.
     * @param[in] directory Directory which holds the traced modules.
     */
    ImplementationResolver(const ModuleImages& modules, std::filesystem::path directory)
        : modules_(modules), directory_(std::move(directory))
    {
    }

    /**
     * @brief Resolve a symbol to the address of its implementation.
     *
     * @param[in] module Module which exports the name (lowercase base name).
     * @param[in] name Exported name.
     * @param[out] target_module Module which holds the implementation.
     * @param[out] target_rva Offset of the implementation.
     * @return Whether the implementation could be determined.
     */
    bool Resolve(const std::wstring& module, const std::wstring& name, std::wstring& target_module,
                 std::uint32_t& target_rva)
    {
        return ResolveRecursive(module, name, target_module, target_rva, 0);
    }

    /**
     * @brief Report whether an address may carry a breakpoint.
     *
     * @param[in] module Module which holds the address.
     * @param[in] rva Offset of the address.
     * @return Whether the address is inside an executable section.
     */
    bool IsExecutable(const std::wstring& module, std::uint32_t rva)
    {
        const PeImage* image = FindImage(module);
        return image != nullptr && image->IsExecutable(rva);
    }

private:
    /**
     * @brief Find an already parsed image.
     *
     * @param[in] module Module to look up.
     * @return The image, or nullptr when it was not parsed yet.
     */
    const PeImage* FindImage(const std::wstring& module)
    {
        const auto traced = modules_.find(module);
        if (traced != modules_.end())
        {
            return &traced->second.image;
        }

        const auto extra = extra_images_.find(module);
        return extra == extra_images_.end() ? nullptr : &extra->second;
    }

    /**
     * @brief Parse a module which is not part of the traced set.
     *
     * @param[in] module Module to parse.
     * @return The image, or nullptr when the module can not be read.
     */
    const PeImage* LoadImage(const std::wstring& module)
    {
        const auto known = missing_images_.find(module);
        if (known != missing_images_.end())
        {
            return nullptr;
        }

        if (directory_.empty())
        {
            missing_images_.insert(module);
            return nullptr;
        }

        try
        {
            const auto inserted =
                extra_images_.emplace(module, PeImage::FromFile(directory_ / (module + L".dll")));
            return &inserted.first->second;
        }
        catch (const std::runtime_error&)
        {
            /* Remember the failure: the same module is looked up once per name. */
            missing_images_.insert(module);
            return nullptr;
        }
    }

    /**
     * @brief Follow a forwarder chain.
     *
     * @param[in] module Module which exports the name.
     * @param[in] name Exported name.
     * @param[out] target_module Module which holds the implementation.
     * @param[out] target_rva Offset of the implementation.
     * @param[in] depth Current depth of the chain.
     * @return Whether the implementation could be determined.
     */
    bool ResolveRecursive(const std::wstring& module, const std::wstring& name,
                          std::wstring& target_module, std::uint32_t& target_rva, int depth)
    {
        if (depth > kMaxForwarderDepth)
        {
            return false;
        }

        const PeImage* image = FindImage(module);
        if (image == nullptr)
        {
            image = LoadImage(module);
        }

        if (image == nullptr)
        {
            return false;
        }

        const ExportEntry* entry = nullptr;
        for (const auto& candidate : image->Exports())
        {
            if (candidate.name == name)
            {
                entry = &candidate;
                break;
            }
        }

        if (entry == nullptr)
        {
            return false;
        }

        if (entry->forwarder.empty())
        {
            target_module = module;
            target_rva = entry->rva;
            return true;
        }

        std::wstring forwarded_module;
        std::wstring forwarded_name;
        if (!SplitForwarder(entry->forwarder, forwarded_module, forwarded_name))
        {
            return false;
        }

        return ResolveRecursive(forwarded_module, forwarded_name, target_module, target_rva, depth + 1);
    }

    const ModuleImages& modules_;                       ///< Traced modules.
    std::filesystem::path directory_;                   ///< Directory of the modules.
    std::map<std::wstring, PeImage> extra_images_;      ///< Images parsed on demand.
    std::set<std::wstring> missing_images_;             ///< Modules which could not be parsed.
};

/**
 * @brief Render a 64 bit value as lowercase hexadecimal without a prefix.
 *
 * @param[in] value Value to render.
 * @return The hexadecimal text.
 */
std::string ToHex(std::uint64_t value)
{
    if (value == 0)
    {
        return "0";
    }

    std::string text;
    while (value != 0)
    {
        const unsigned digit = static_cast<unsigned>(value & 0xFU);
        text.push_back(static_cast<char>(digit < 10U ? ('0' + digit) : ('a' + digit - 10U)));
        value >>= 4U;
    }

    std::reverse(text.begin(), text.end());
    return text;
}

} // namespace

std::vector<ArmGroup> BuildArmPlan(const ModuleImages& modules,
                                   const std::vector<Category>& categories, bool all_exports,
                                   const std::filesystem::path& module_directory)
{
    ImplementationResolver resolver(modules, module_directory);
    std::map<std::pair<std::wstring, std::uint32_t>, std::set<std::wstring>> groups;

    for (const auto& traced : modules)
    {
        const std::wstring& module = traced.first;
        for (const auto& entry : traced.second.image.Exports())
        {
            if (!IsSafeExportName(entry.name))
            {
                continue;
            }

            if (!all_exports && !IsInScope(entry.name, categories))
            {
                continue;
            }

            std::wstring target_module;
            std::uint32_t target_rva = 0;
            if (entry.forwarder.empty())
            {
                target_module = module;
                target_rva = entry.rva;
            }
            else if (!resolver.Resolve(module, entry.name, target_module, target_rva))
            {
                /* A forwarder which leaves the readable modules can not be armed. */
                continue;
            }

            if (!resolver.IsExecutable(target_module, target_rva))
            {
                continue;
            }

            groups[{target_module, target_rva}].insert(module + L"!" + entry.name);
        }
    }

    std::vector<ArmGroup> plan;
    plan.reserve(groups.size());
    for (auto& group : groups)
    {
        ArmGroup entry;
        entry.module = group.first.first;
        entry.rva = group.first.second;
        entry.names.assign(group.second.begin(), group.second.end());
        plan.push_back(std::move(entry));
    }

    return plan;
}

std::vector<std::string> BuildArmLines(const std::vector<ArmGroup>& plan, const ModuleBases& bases)
{
    std::vector<std::string> lines;
    lines.reserve(plan.size());

    for (const auto& group : plan)
    {
        const auto base = bases.find(group.module);
        if (base == bases.end())
        {
            continue;
        }

        std::string line = "bp /1 0x";
        line += ToHex(base->second + group.rva);
        line += " \".echo ";
        line += kHitMarker;

        for (const auto& name : group.names)
        {
            line += ' ';
            line += appbox::WideToUTF8(name);
        }

        line += "; g\"";
        lines.push_back(std::move(line));
    }

    return lines;
}

} // namespace appbox::tracer

#ifndef APPBOX_TEST_UTILS_MODULE_LIST_HPP
#define APPBOX_TEST_UTILS_MODULE_LIST_HPP

#include <cctype>
#include <string>
#include <vector>

namespace appbox::test
{

/**
 * @brief Lowercase copy of a module name.
 *
 * Module names are ASCII, so the conversion of the default locale is enough to
 * compare two names which differ in the case only.
 *
 * @param[in] name Base name of a module.
 * @return The name with every upper case letter replaced by its lower case one.
 */
inline std::string LowercaseModuleName(const std::string& name)
{
    std::string lower;
    lower.reserve(name.size());
    for (const char ch : name)
    {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }

    return lower;
}

/**
 * @brief Whether a list of module names carries one module.
 *
 * @param[in] modules Base names of the modules.
 * @param[in] name Name to look for; the comparison ignores the case.
 * @return Whether the list carries the module.
 */
inline bool HasModule(const std::vector<std::string>& modules, const std::string& name)
{
    const auto lower = LowercaseModuleName(name);
    for (const auto& module : modules)
    {
        if (LowercaseModuleName(module) == lower)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Whether a module is one the Visual C++ Redistributable installs.
 *
 * The prefixes name the modules a machine has to carry for a program which was
 * linked against the DLL form of the runtime library: `VCRUNTIME` (the
 * `vcruntime140.dll` family, `vcruntime140_1.dll` included), `MSVCP` (the
 * `msvcp140*.dll` family), `CONCRT` and `VCCORLIB`. The universal CRT
 * (`ucrtbase.dll`, the `api-ms-win-crt-*` API sets), the legacy C runtime
 * (`msvcrt.dll`) and `msvcp_win.dll` are deliberately not part of the list:
 * they belong to Windows, so a process which loads them needs no installation,
 * and the modules of the operating system a process loads import them
 * themselves. `msvcp_win.dll` is named `MSVCP` as well, which is why the
 * redistributable family is matched by `msvcp140` and not by `msvcp`.
 *
 * @param[in] name Base name of a module.
 * @return true when the module needs the redistributable of the toolchain.
 */
inline bool IsVcRedistributableModule(const std::string& name)
{
    static constexpr const char* kPrefixes[] = {
        "vcruntime",
        "msvcp140",
        "concrt",
        "vccorlib",
    };

    const auto lower = LowercaseModuleName(name);
    for (const char* prefix : kPrefixes)
    {
        if (lower.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }

    return false;
}

/**
 * @brief Whether a module belongs to the runtime library of the toolchain.
 *
 * This is the strict form of the check above: it names every module which
 * carries a part of the C++ runtime, the parts of the operating system
 * included (`ucrtbase.dll`, the `api-ms-win-crt-*` API sets, `msvcrt.dll` and
 * `msvcp_win.dll`). A product which links the runtime statically imports none
 * of them, so the check is applied to the import tables of the products of the
 * build; the module list of a running process is checked with
 * IsVcRedistributableModule() instead, because the modules of Windows which it
 * loads import the runtime of the operating system themselves.
 *
 * @param[in] name Base name of a module.
 * @return true when the module belongs to the runtime library of the toolchain.
 */
inline bool IsVcRuntimeModule(const std::string& name)
{
    static constexpr const char* kPrefixes[] = {
        "vcruntime", "msvcp", "concrt", "vccorlib", "ucrtbase", "api-ms-win-crt-", "msvcrt",
    };

    const auto lower = LowercaseModuleName(name);
    for (const char* prefix : kPrefixes)
    {
        if (lower.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }

    return false;
}

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_MODULE_LIST_HPP

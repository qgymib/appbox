#ifndef APPBOX_TRACER_SCOPEPATTERNS_HPP
#define APPBOX_TRACER_SCOPEPATTERNS_HPP

#include "tracer/Category.hpp"
#include <string>
#include <vector>

namespace appbox::tracer
{

/**
 * @brief One entry of the default scope: one exported name of one module.
 *
 * The scope is the set of lowest level entry points of the three isolation
 * domains, which is what a debugger run reports. It is deliberately independent
 * of the hooks the sandbox implements: the sandbox may cover a part of the set,
 * and the tracer still reports the whole domain.
 */
struct ScopeEntry
{
    const wchar_t* module;   ///< Lowercase base name of the exporting module.
    Category       category; ///< Domain the name belongs to.
    const wchar_t* name;     ///< Exported name; `Nt*` is canonical, `Zw*` follows by rule.
};

/**
 * @brief Every entry of the default scope.
 *
 * The table is the single source of truth of the scope: it is the list the
 * classifier is built from, and the unit tests verify it against the export
 * tables of the modules it names. The names are grouped per domain and per
 * module, in the order they are documented.
 *
 * @return The entries, in table order.
 */
std::vector<ScopeEntry> ScopeTable();

/**
 * @brief Decide which categories an export of one module belongs to.
 *
 * The decision is a table lookup, because a name pattern is far too broad: a
 * keyword match cannot tell a file section from an ALPC section
 * (`NtAlpcCreatePortSection`), a registry key from a synchronization object
 * (`NtCreateKeyedEvent`) or an object manager directory from a file directory
 * (`NtOpenDirectoryObject`). Only the lowest level entry points of a domain are
 * listed: the Win32 wrappers (`CreateFileW`, `RegOpenKeyExW`, `CreateNamedPipeW`
 * and alike) and the `Rtl*` path helpers are not part of the scope, they are
 * only reachable through `--all-exports`.
 *
 * The `Zw` alias of an NT entry point inherits the categories of its `Nt`
 * counterpart, because both names share one address and one implementation.
 *
 * An exported name can belong to more than one category: `NtDeviceIoControlFile`
 * is the file I/O entry point and the way socket requests reach the kernel, so
 * it is a file and a network function at the same time.
 *
 * @param[in] module Lowercase base name of the exporting module (`ntdll`).
 * @param[in] name Exported name of the function, without the module prefix.
 * @return The categories of the name, empty when it is not in the scope.
 */
std::vector<Category> ClassifyExport(const std::wstring& module, const std::wstring& name);

/**
 * @brief Report whether an exported name of one module belongs to one category.
 *
 * @param[in] module Lowercase base name of the exporting module (`ntdll`).
 * @param[in] name Exported name of the function, without the module prefix.
 * @param[in] category Category to test.
 * @return Whether the name is part of the category.
 */
bool MatchesCategory(const std::wstring& module, const std::wstring& name, Category category);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_SCOPEPATTERNS_HPP

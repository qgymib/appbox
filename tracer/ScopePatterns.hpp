#ifndef APPBOX_TRACER_SCOPEPATTERNS_HPP
#define APPBOX_TRACER_SCOPEPATTERNS_HPP

#include "tracer/Options.hpp"
#include <string>
#include <vector>

namespace appbox::tracer
{

/**
 * @brief Decide which categories an exported function belongs to.
 *
 * The classification is a table of explicit rules, because a plain substring
 * match is far too broad: `reg` matches `EtwEventRegister`, `connect` matches
 * `DbgUiConnectToDbg`, `directory` matches `NtCreateDirectoryObject` and `key`
 * matches the synchronization object `NtCreateKeyedEvent`.
 *
 * An exported name can belong to more than one category: `NtDeviceIoControlFile`
 * is the file I/O entry point and the way socket requests reach the kernel, so
 * it is a file and a network function at the same time.
 *
 * @param[in] name Exported name of the function, without the module prefix.
 * @return The categories of the name, empty when it is not in the scope.
 */
std::vector<Category> ClassifyExport(const std::wstring& name);

/**
 * @brief Report whether an exported name belongs to one category.
 *
 * @param[in] name Exported name of the function, without the module prefix.
 * @param[in] category Category to test.
 * @return Whether the name is part of the category.
 */
bool MatchesCategory(const std::wstring& name, Category category);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_SCOPEPATTERNS_HPP

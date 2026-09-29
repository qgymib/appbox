#include "sandbox/utils/WinAPI.h" /* Must be first include file */
#include <fstream>
#include <nlohmann/json.hpp>
#include "SandboxLayout.hpp"
#include "WString.hpp"
#include "EnvironmentIsolationBuilder.hpp"

namespace
{

/**
 * @brief Write a text as a file of the fixed layout of a case.
 * @param[in] path Path of the file.
 * @param[in] text Text of the file.
 * @return true on success.
 */
bool WriteText(const std::filesystem::path& path, const std::string& text)
{
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec)
    {
        return false;
    }

    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    return stream.good();
}

/**
 * @brief Path of the environment isolation file of a test sandbox.
 * @param[in] case_root Root directory of the case.
 * @return The path of the file inside the resources of the case.
 */
std::filesystem::path IsolationFilePath(const std::filesystem::path& case_root)
{
    return case_root / appbox::layout::kAppDirNameW / appbox::layout::kEnvironmentDirNameW /
           appbox::layout::kIsolationFileNameW;
}

/**
 * @brief Path of the environment state file of a test sandbox.
 * @param[in] case_root Root directory of the case.
 * @return The path of the file inside the state directory of the case.
 */
std::filesystem::path StateFilePath(const std::filesystem::path& case_root)
{
    return case_root / appbox::layout::kStateDirNameW / appbox::layout::kEnvironmentDirNameW /
           appbox::layout::kEnvironmentStateFileNameW;
}

} // namespace

bool appbox::test::WriteEnvironmentIsolationFile(const std::filesystem::path&                  case_root,
                                                 const std::vector<EnvironmentIsolationEntry>& entries)
{
    return WriteText(IsolationFilePath(case_root), BuildEnvironmentIsolationText(entries));
}

std::string appbox::test::BuildEnvironmentIsolationText(const std::vector<EnvironmentIsolationEntry>& entries)
{
    /*
     * The document is filled as the structure of the schema of the file, so a
     * case writes the same document the workspace writes.
     */
    appbox::environment_isolation::Document document;

    for (const auto& entry : entries)
    {
        appbox::environment_isolation::Entry item;
        item.name = appbox::WideToUTF8(entry.name);
        item.value = appbox::WideToUTF8(entry.value);
        item.isolation = entry.isolation;
        item.merge = entry.merge;
        item.merge_string = appbox::WideToUTF8(entry.merge_string);
        document.entries.push_back(std::move(item));
    }

    return nlohmann::json(document).dump(2);
}

bool appbox::test::WriteEnvironmentIsolationFileText(const std::filesystem::path& case_root, const std::string& text)
{
    return WriteText(IsolationFilePath(case_root), text);
}

bool appbox::test::WriteEnvironmentStateFile(const std::filesystem::path&              case_root,
                                             const std::vector<EnvironmentStateEntry>& entries)
{
    /*
     * The state document is filled as the structure of its schema, which is the
     * one the sandbox writes while the application changes its environment.
     */
    appbox::environment_isolation::StateDocument document;

    for (const auto& entry : entries)
    {
        appbox::environment_isolation::StateEntry item;
        item.name = appbox::WideToUTF8(entry.name);
        item.value = appbox::WideToUTF8(entry.value);
        item.deleted = entry.deleted;
        document.entries.push_back(std::move(item));
    }

    return WriteText(StateFilePath(case_root), nlohmann::json(document).dump(2));
}

appbox::test::HostEnvironmentVariable::HostEnvironmentVariable(const std::wstring& name, const std::wstring& value)
    : name_(name)
{
    ::SetEnvironmentVariableW(name_.c_str(), value.c_str());
}

appbox::test::HostEnvironmentVariable::~HostEnvironmentVariable()
{
    ::SetEnvironmentVariableW(name_.c_str(), nullptr);
}

std::wstring appbox::test::HostEnvironmentVariable::Value() const
{
    std::vector<wchar_t> buffer(1024);
    const DWORD length = ::GetEnvironmentVariableW(name_.c_str(), buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0)
    {
        return std::wstring();
    }

    return std::wstring(buffer.data(), length);
}

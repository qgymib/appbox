#ifndef APPBOX_TEST_CASES_INIT_HPP
#define APPBOX_TEST_CASES_INIT_HPP

#include <filesystem>
#include <memory>
#include <string>

namespace appbox::test
{

struct CWD
{
    typedef std::shared_ptr<CWD> Ptr;

    CWD() = default;
    ~CWD();

    /**
     * @brief Create a new CWD instance.
     * @param[in] id The unique ID of the CWD instance.
     * @param[in] cleanup Whether to delete the CWD directory when the CWD instance is destroyed.
     * @return The CWD instance pointe
     */
    static Ptr Create(const std::wstring& id, bool cleanup = true);

    /**
     * @brief Get the path of the CWD directory.
     * @return The path of the CWD directory.
     */
    std::wstring WString() const;

    /**
     * @brief Stop cleaning up the CWD directory when the CWD instance is destroyed.
     */
    void NoCleanup(bool yes);

    CWD(const CWD&) = delete;
    CWD& operator=(const CWD&) = delete;
    CWD(CWD&&) = delete;
    CWD& operator=(CWD&&) = delete;

    bool                  cleanup_;
    std::filesystem::path cwd_;
};

} // namespace appbox::test

#endif

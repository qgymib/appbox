#ifndef APPBOX_TEST_UTILS_REAL_HKCU_KEY_HPP
#define APPBOX_TEST_UTILS_REAL_HKCU_KEY_HPP

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include <string>

namespace appbox::test
{

/**
 * @brief RAII helper which owns a key below the real HKCU of the test process.
 *
 * The key is created by the test process outside the sandbox and removed again
 * when the helper goes out of scope, so a test which needs a host entry does
 * not leave anything behind.
 */
class RealHkcuKey
{
public:
    /**
     * @brief Create the key below HKCU.
     * @param[in] subkey Path of the key below HKCU, for example
     *                   `L"Software\\AppBoxTest\\Case"`.
     */
    explicit RealHkcuKey(const std::wstring& subkey);

    ~RealHkcuKey();

    RealHkcuKey(const RealHkcuKey&) = delete;
    RealHkcuKey& operator=(const RealHkcuKey&) = delete;
    RealHkcuKey(RealHkcuKey&&) = delete;
    RealHkcuKey& operator=(RealHkcuKey&&) = delete;

    /**
     * @brief Write a `REG_SZ` value into the key.
     * @param[in] name Name of the value.
     * @param[in] value Text of the value.
     * @return true on success.
     */
    bool SetString(const std::wstring& name, const std::wstring& value);

    /**
     * @brief Write a `REG_DWORD` value into the key.
     * @param[in] name Name of the value.
     * @param[in] value Data of the value.
     * @return true on success.
     */
    bool SetDword(const std::wstring& name, DWORD value);

    /**
     * @brief The handle of the key.
     * @return The handle, nullptr when the key could not be created.
     */
    HKEY get() const;

private:
    std::wstring subkey_;
    HKEY         key_ = nullptr;
};

} // namespace appbox::test

#endif // APPBOX_TEST_UTILS_REAL_HKCU_KEY_HPP

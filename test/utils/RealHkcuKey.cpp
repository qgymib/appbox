#include "RealHkcuKey.hpp"
#include <cstring>

appbox::test::RealHkcuKey::RealHkcuKey(const std::wstring& subkey) : subkey_(subkey)
{
    DWORD disposition = 0;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, subkey_.c_str(), 0, nullptr, 0, KEY_ALL_ACCESS, nullptr, &key_,
                        &disposition) != ERROR_SUCCESS)
    {
        key_ = nullptr;
    }
}

appbox::test::RealHkcuKey::~RealHkcuKey()
{
    if (key_ != nullptr)
    {
        RegCloseKey(key_);
    }

    RegDeleteTreeW(HKEY_CURRENT_USER, subkey_.c_str());
}

bool appbox::test::RealHkcuKey::SetString(const std::wstring& name, const std::wstring& value)
{
    if (key_ == nullptr)
    {
        return false;
    }

    return RegSetValueExW(key_, name.c_str(), 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
}

bool appbox::test::RealHkcuKey::SetDword(const std::wstring& name, DWORD value)
{
    if (key_ == nullptr)
    {
        return false;
    }

    return RegSetValueExW(key_, name.c_str(), 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value), sizeof(value))
           == ERROR_SUCCESS;
}

HKEY appbox::test::RealHkcuKey::get() const
{
    return key_;
}

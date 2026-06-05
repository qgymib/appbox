#include "sandbox/utils/WinAPI.h"
#include "Registry.hpp"
#include <stdexcept>

struct appbox::RegistryHive::Data
{
    Data();
    ~Data();
    HKEY hKey;
};

appbox::RegistryHive::Data::Data()
{
    hKey = nullptr;
}

appbox::RegistryHive::Data::~Data()
{
    if (hKey != nullptr)
    {
        RegCloseKey(hKey);
        hKey = nullptr;
    }
}

appbox::RegistryHive::RegistryHive()
{
    data_ = new Data;
}

appbox::RegistryHive::~RegistryHive()
{
    delete data_;
}

appbox::RegistryHive::Ptr appbox::RegistryHive::Create(const std::wstring& path)
{
    appbox::RegistryHive::Ptr obj(new RegistryHive);

    auto ret = RegLoadAppKeyW(path.c_str(), &obj->data_->hKey, KEY_ALL_ACCESS, 0, 0);
    if (ret != ERROR_SUCCESS)
    {
        throw std::runtime_error("Failed to load registry hive.");
    }

    return obj;
}

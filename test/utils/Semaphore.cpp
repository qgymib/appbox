#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
#include "Semaphore.hpp"

struct appbox::Semaphore::Data
{
    Data();
    ~Data();
    HANDLE sem_;
};

appbox::Semaphore::Data::Data()
{
    sem_ = CreateSemaphore(nullptr, 0, INT_MAX, nullptr);
}

appbox::Semaphore::Data::~Data()
{
    CloseHandle(sem_);
}

appbox::Semaphore::Semaphore()
{
    data_ = new Data;
}

appbox::Semaphore::~Semaphore()
{
    delete data_;
}

void appbox::Semaphore::Release(size_t count)
{
    ReleaseSemaphore(data_->sem_, (LONG)count, nullptr);
}

bool appbox::Semaphore::Acquire(uint32_t timeout_ms)
{
    DWORD t = (timeout_ms == (uint32_t)-1) ? INFINITE : timeout_ms;
    return WaitForSingleObject(data_->sem_, t) == WAIT_OBJECT_0;
}

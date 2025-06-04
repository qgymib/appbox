#ifndef APPBOX_TEST_HONEYPOT_INIT_HPP
#define APPBOX_TEST_HONEYPOT_INIT_HPP

#include <string>

namespace appbox
{

struct TestCase
{
    const wchar_t* name; /* Test name. */
    void (*test)();      /* Test body. */
};

extern const TestCase CreateProcessA;

void ForeachTestCase(void (*cb)(const TestCase* test, void* arg), void* arg);

int RunTestCase(const std::wstring& pattern);

} // namespace appbox

#endif

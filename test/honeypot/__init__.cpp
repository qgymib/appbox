#include "utils/macros.hpp"
#include "utils/wstring.hpp"
#include "__init__.hpp"
#include <stdexcept>
#include <shlwapi.h>

static const appbox::TestCase* s_tests[] = {
    &appbox::CreateProcessA,
};

static void OnRunTest(const appbox::TestCase* test, void* arg)
{
    const std::wstring* pattern = (std::wstring*)arg;
    if (PathMatchSpecW(test->name, pattern->c_str()))
    {
        try
        {
            wprintf(L"[ RUN      ] %s\n", test->name);
            test->test();
            wprintf(L"[       OK ] %s\n", test->name);
        }
        catch (const std::runtime_error& e)
        {
            std::wstring msg = appbox::mbstowcs(e.what(), CP_UTF8);
            wprintf(L"%s\n", msg.c_str());
            wprintf(L"[   FAILED ] %s\n", test->name);
            exit(EXIT_FAILURE);
        }
    }
}

void appbox::ForeachTestCase(void (*cb)(const TestCase* test, void* arg), void* arg)
{
    for (size_t i = 0; i < ARRAY_SIZE(s_tests); i++)
    {
        cb(s_tests[i], arg);
    }
}

int appbox::RunTestCase(const std::wstring& pattern)
{
    ForeachTestCase(OnRunTest, (void*)&pattern);
    return 0;
}

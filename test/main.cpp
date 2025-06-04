#include "utils/winapi.hpp"
#include "honeypot/__init__.hpp"

static const wchar_t* s_help = L""
"--test_filter=[PATTERN]\n"
"  Run all tests match the filter.\n"
"--test_list_all_tests\n"
"  List all tests and exit.\n"
"-h,--help\n"
"  Show this help and exit.\n"
;

static void OnListTestCase(const appbox::TestCase* test, void*)
{
    wprintf(L"%s\n", test->name);
}

int wmain(int argc, wchar_t* argv[])
{
    std::wstring pattern = L"*";

    const wchar_t* opt;
    size_t opt_len;
    for (int i = 0; i < argc; i++)
    {
        if (wcscmp(argv[i], L"-h") == 0 || wcscmp(argv[i], L"--help") == 0)
        {
            wprintf(L"%s", s_help);
            exit(EXIT_SUCCESS);
        }
        if (wcscmp(argv[i], L"--test_list_all_tests") == 0)
        {
            appbox::ForeachTestCase(OnListTestCase, nullptr);
            exit(EXIT_SUCCESS);
        }

        opt = L"--test_filter=";
        opt_len = wcslen(opt);
        if (wcsncmp(argv[i], opt, opt_len) == 0)
        {
            pattern = argv[i] + opt_len;
            continue;
        }
    }

    return appbox::RunTestCase(pattern);
}

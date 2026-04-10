#include "BuildCommandLine.hpp"

// 按照 CommandLineToArgvW 的反向规则对单个参数进行引用
static std::wstring QuoteArgument(const std::wstring& arg)
{
    // 如果参数为空或包含空格、制表符、双引号，则需要引用
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos)
    {
        return arg;
    }

    std::wstring quoted = L"\"";
    for (auto it = arg.begin();; ++it)
    {
        size_t num_backslashes = 0;

        while (it != arg.end() && *it == L'\\')
        {
            ++it;
            ++num_backslashes;
        }

        if (it == arg.end())
        {
            // 参数末尾：反斜杠需要翻倍（因为后面紧跟的是闭合的 "）
            quoted.append(num_backslashes * 2, L'\\');
            break;
        }
        else if (*it == L'"')
        {
            // 反斜杠翻倍，再加转义的引号
            quoted.append(num_backslashes * 2 + 1, L'\\');
            quoted.push_back(*it);
        }
        else
        {
            // 反斜杠后面不是引号，原样保留
            quoted.append(num_backslashes, L'\\');
            quoted.push_back(*it);
        }
    }
    quoted.push_back(L'"');
    return quoted;
}

std::wstring appbox::BuildCommandLine(const std::wstring&              exe,
                                      const std::vector<std::wstring>& args)
{
    std::wstring cmdline = QuoteArgument(exe);
    for (const auto& arg : args)
    {
        cmdline += L' ';
        cmdline += QuoteArgument(arg);
    }
    return cmdline;
}

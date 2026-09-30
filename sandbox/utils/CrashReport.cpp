#include "utils/WinAPI.h" /* Must be first include file */
#include <tlhelp32.h>
#include <cstddef>
#include "utils/Log.hpp"
#include "utils/LogFile.hpp"
#include "WString.hpp"
#include "CrashReport.hpp"

namespace
{

/** Number of the modules the report can name. */
constexpr std::size_t kMaxModules = 512;

/** Number of the characters of a module name which are kept. */
constexpr std::size_t kModuleNameSize = 64;

/** Number of the frames the report walks. */
constexpr std::size_t kMaxFrames = 32;

/** Capacity of the buffer which holds one line of the report. */
constexpr std::size_t kTextCapacity = 512;

/**
 * @brief One module of the process, as it was loaded when the handler was installed.
 */
struct ModuleRecord
{
    unsigned long long base;                  /* Address the module is loaded at. */
    unsigned long long end;                   /* Address behind the module. */
    wchar_t            name[kModuleNameSize]; /* Name of the module. */
};

ModuleRecord s_modules[kMaxModules];
std::size_t  s_module_count = 0;

/** Whether a report is being written, so a fault of the report itself does not recurse. */
volatile LONG s_reporting = 0;

/**
 * @brief Buffer which holds one line of the report.
 *
 * The report must not allocate memory, so the text is built in a buffer of the
 * stack and written once it is complete.
 */
struct Text
{
    char        data[kTextCapacity];
    std::size_t size = 0;
};

/**
 * @brief Append a text.
 * @param[in,out] text Buffer which receives the text.
 * @param[in] value Text to append.
 */
void AppendText(Text& text, const char* value)
{
    if (value == nullptr)
    {
        return;
    }

    for (std::size_t index = 0; value[index] != '\0' && text.size < kTextCapacity; ++index)
    {
        text.data[text.size++] = value[index];
    }
}

/**
 * @brief Append a number in hexadecimal form.
 * @param[in,out] text Buffer which receives the number.
 * @param[in] value Number to append.
 */
void AppendHex(Text& text, unsigned long long value)
{
    static const char kDigits[] = "0123456789abcdef";

    char        buffer[16];
    std::size_t count = 0;
    do
    {
        buffer[count++] = kDigits[value & 0xF];
        value >>= 4;
    } while (value != 0 && count < sizeof(buffer));

    while (count > 0 && text.size < kTextCapacity)
    {
        text.data[text.size++] = buffer[--count];
    }
}

/**
 * @brief Append a number in decimal form.
 * @param[in,out] text Buffer which receives the number.
 * @param[in] value Number to append.
 */
void AppendDec(Text& text, unsigned long long value)
{
    char        buffer[20];
    std::size_t count = 0;
    do
    {
        buffer[count++] = static_cast<char>('0' + (value % 10));
        value /= 10;
    } while (value != 0 && count < sizeof(buffer));

    while (count > 0 && text.size < kTextCapacity)
    {
        text.data[text.size++] = buffer[--count];
    }
}

/**
 * @brief Append a name of a module.
 *
 * The names are read before the process can crash, so the report never asks
 * the loader for a name. A character outside of the printable ASCII range is
 * written as `?`, which keeps the report readable whatever the name holds.
 *
 * @param[in,out] text Buffer which receives the name.
 * @param[in] value Name to append.
 */
void AppendWideText(Text& text, const wchar_t* value)
{
    if (value == nullptr)
    {
        return;
    }

    for (std::size_t index = 0; value[index] != L'\0' && text.size < kTextCapacity; ++index)
    {
        const wchar_t character = value[index];
        text.data[text.size++] = (character >= 0x20 && character < 0x7F) ? static_cast<char>(character) : '?';
    }
}

/**
 * @brief Write the line the buffer holds and start the next one.
 * @param[in,out] text Buffer to write.
 */
void Flush(Text& text)
{
    appbox::LogFile::Write(text.data, text.size);
    text.size = 0;
}

/**
 * @brief Append an address in hexadecimal form.
 * @param[in,out] text Buffer which receives the address.
 * @param[in] address Address to append.
 */
void AppendAddress(Text& text, unsigned long long address)
{
    AppendText(text, "0x");
    AppendHex(text, address);
}

/**
 * @brief Append an address and the module it belongs to.
 * @param[in,out] text Buffer which receives the address.
 * @param[in] address Address to append.
 */
void AppendFrame(Text& text, unsigned long long address)
{
    AppendText(text, "  ");
    AppendAddress(text, address);

    for (std::size_t index = 0; index < s_module_count; ++index)
    {
        if (address < s_modules[index].base || address >= s_modules[index].end)
        {
            continue;
        }

        AppendText(text, " ");
        AppendWideText(text, s_modules[index].name);
        AppendText(text, "+0x");
        AppendHex(text, address - s_modules[index].base);
        return;
    }

    AppendText(text, " <unknown>");
}

/**
 * @brief Whether an exception is used to control the flow of a program.
 *
 * Such an exception is not a failure: the runtime of a program raises it to
 * report a C++ exception, to name a thread or to print a debug message. The
 * report stays silent about them, so a real failure is not buried under the
 * exceptions of a healthy run.
 *
 * @param[in] code Code of the exception.
 * @return true when the exception carries no failure.
 */
bool IsControlFlowException(DWORD code)
{
    switch (code)
    {
    case 0x40010006: /* DBG_PRINTEXCEPTION_C */
    case 0x4001000A: /* DBG_PRINTEXCEPTION_WIDE_C */
    case 0x406D1388: /* MS_VC_EXCEPTION, the name a thread is given */
    case 0x80000003: /* EXCEPTION_BREAKPOINT */
    case 0x80000004: /* EXCEPTION_SINGLE_STEP */
    case 0xE06D7363: /* A C++ exception of the MSVC runtime */
        return true;
    default:
        return false;
    }
}

/**
 * @brief Collect the modules of the process.
 *
 * The table is read while the handler is installed: the loader of the process
 * takes a lock for it, which the handler itself must not do.
 */
void CollectModules()
{
    const HANDLE snapshot =
        ::CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, ::GetCurrentProcessId());
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return;
    }

    MODULEENTRY32W entry;
    ZeroMemory(&entry, sizeof(entry));
    entry.dwSize = sizeof(entry);

    if (::Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (s_module_count >= kMaxModules)
            {
                break;
            }

            ModuleRecord& record = s_modules[s_module_count++];
            record.base = reinterpret_cast<unsigned long long>(entry.modBaseAddr);
            record.end = record.base + entry.modBaseSize;

            std::size_t index = 0;
            for (; index < kModuleNameSize - 1 && entry.szModule[index] != L'\0'; ++index)
            {
                record.name[index] = entry.szModule[index];
            }
            record.name[index] = L'\0';
        } while (::Module32NextW(snapshot, &entry));
    }

    ::CloseHandle(snapshot);
}

/**
 * @brief Append the registers of the context of the exception.
 * @param[in,out] text Buffer which receives the registers.
 * @param[in] context Context of the exception.
 */
void AppendRegisters(Text& text, const CONTEXT& context)
{
#if defined(_M_X64)
    AppendText(text, "rip=");
    AppendAddress(text, context.Rip);
    AppendText(text, " rsp=");
    AppendAddress(text, context.Rsp);
    AppendText(text, " rbp=");
    AppendAddress(text, context.Rbp);
    AppendText(text, " rax=");
    AppendAddress(text, context.Rax);
    AppendText(text, " rbx=");
    AppendAddress(text, context.Rbx);
    AppendText(text, " rcx=");
    AppendAddress(text, context.Rcx);
    AppendText(text, " rdx=");
    AppendAddress(text, context.Rdx);
    AppendText(text, " rsi=");
    AppendAddress(text, context.Rsi);
    AppendText(text, " rdi=");
    AppendAddress(text, context.Rdi);
    AppendText(text, " r8=");
    AppendAddress(text, context.R8);
    AppendText(text, " r9=");
    AppendAddress(text, context.R9);
    AppendText(text, " r10=");
    AppendAddress(text, context.R10);
    AppendText(text, " r11=");
    AppendAddress(text, context.R11);
    AppendText(text, " r12=");
    AppendAddress(text, context.R12);
    AppendText(text, " r13=");
    AppendAddress(text, context.R13);
    AppendText(text, " r14=");
    AppendAddress(text, context.R14);
    AppendText(text, " r15=");
    AppendAddress(text, context.R15);
#else
    AppendText(text, "eip=");
    AppendAddress(text, context.Eip);
    AppendText(text, " esp=");
    AppendAddress(text, context.Esp);
    AppendText(text, " ebp=");
    AppendAddress(text, context.Ebp);
    AppendText(text, " eax=");
    AppendAddress(text, context.Eax);
    AppendText(text, " ebx=");
    AppendAddress(text, context.Ebx);
    AppendText(text, " ecx=");
    AppendAddress(text, context.Ecx);
    AppendText(text, " edx=");
    AppendAddress(text, context.Edx);
    AppendText(text, " esi=");
    AppendAddress(text, context.Esi);
    AppendText(text, " edi=");
    AppendAddress(text, context.Edi);
#endif
}

/**
 * @brief Append the instruction pointer of the context of the exception.
 * @param[in,out] text Buffer which receives the instruction pointer.
 * @param[in] context Context of the exception.
 */
void AppendInstructionPointer(Text& text, const CONTEXT& context)
{
#if defined(_M_X64)
    AppendAddress(text, context.Rip);
#else
    AppendAddress(text, context.Eip);
#endif
}

/**
 * @brief Write the report of one exception to the log file of the process.
 *
 * The function is called from a handler of the process which is crashing, so
 * it only reads the records it was given, formats into a buffer of the stack
 * and writes to the log file. It never allocates memory, never takes a lock
 * and never throws.
 *
 * @param[in] info Records of the exception.
 * @param[in] unhandled Whether no handler of the process took the exception.
 */
void Report(PEXCEPTION_POINTERS info, bool unhandled)
{
    if (info == nullptr || info->ExceptionRecord == nullptr || info->ContextRecord == nullptr)
    {
        return;
    }

    if (!appbox::LogFile::IsOpen())
    {
        return;
    }

    /* A fault of the report itself would call the handler again. */
    if (::InterlockedCompareExchange(&s_reporting, 1, 0) != 0)
    {
        return;
    }

    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    const CONTEXT&          context = *info->ContextRecord;

    Text text;

    AppendText(text, "=== crash ===");
    AppendText(text, unhandled ? " unhandled" : " first chance");
    Flush(text);

    AppendText(text, "process=");
    AppendDec(text, ::GetCurrentProcessId());
    AppendText(text, " thread=");
    AppendDec(text, ::GetCurrentThreadId());
    AppendText(text, " code=0x");
    AppendHex(text, record.ExceptionCode);
    AppendText(text, " exception=");
    AppendAddress(text, reinterpret_cast<unsigned long long>(record.ExceptionAddress));
    AppendText(text, " flags=0x");
    AppendHex(text, record.ExceptionFlags);
    Flush(text);

    AppendText(text, "ip=");
    AppendInstructionPointer(text, context);
    Flush(text);

    AppendText(text, "params=");
    for (DWORD index = 0; index < record.NumberParameters && index < EXCEPTION_MAXIMUM_PARAMETERS; ++index)
    {
        if (index != 0)
        {
            AppendText(text, ",");
        }
        AppendAddress(text, record.ExceptionInformation[index]);
    }
    Flush(text);

    AppendRegisters(text, context);
    Flush(text);

    PVOID        frames[kMaxFrames] = {};
    const USHORT count = ::CaptureStackBackTrace(0, static_cast<DWORD>(kMaxFrames), frames, nullptr);

    AppendText(text, "stack=");
    AppendDec(text, count);
    Flush(text);

    for (USHORT index = 0; index < count; ++index)
    {
        AppendFrame(text, reinterpret_cast<unsigned long long>(frames[index]));
        Flush(text);
    }

    AppendText(text, "=== end crash ===");
    Flush(text);

    s_reporting = 0;
}

/**
 * @brief Report an exception before the runtime of the application sees it.
 * @param[in] info Records of the exception.
 * @return `EXCEPTION_CONTINUE_SEARCH`, the sandbox never handles an exception.
 */
LONG NTAPI OnVectoredException(PEXCEPTION_POINTERS info)
{
    if (info != nullptr && info->ExceptionRecord != nullptr &&
        !IsControlFlowException(info->ExceptionRecord->ExceptionCode))
    {
        Report(info, false);
    }

    return EXCEPTION_CONTINUE_SEARCH;
}

/**
 * @brief Report an exception which no handler of the process took.
 * @param[in] info Records of the exception.
 * @return `EXCEPTION_CONTINUE_SEARCH`, the default handler keeps the process.
 */
LONG WINAPI OnUnhandledException(PEXCEPTION_POINTERS info)
{
    Report(info, true);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void appbox::InstallCrashHandler()
{
    CollectModules();

    /*
     * The table is reported once, so the addresses of a report can be
     * symbolized offline with the modules of the build of the run.
     */
    for (std::size_t index = 0; index < s_module_count; ++index)
    {
        LOG_I("module {} base=0x{:x} end=0x{:x}", appbox::WideToUTF8(s_modules[index].name), s_modules[index].base,
              s_modules[index].end);
    }

    ::AddVectoredExceptionHandler(1, OnVectoredException);
    ::SetUnhandledExceptionFilter(OnUnhandledException);
}

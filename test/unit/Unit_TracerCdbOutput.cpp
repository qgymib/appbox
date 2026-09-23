#include <gtest/gtest.h>
#include "tracer/CdbOutputParser.hpp"
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace
{

/** Output of a session start, taken from a real debugger run. */
const char* const kSessionStart = R"RAW(Microsoft (R) Windows Debugger Version 10.0.28000.2705 AMD64
Copyright (c) Microsoft Corporation. All rights reserved.

CommandLine: cmd.exe /c echo hi

************* Path validation summary **************
Response                         Time (ms)     Location
Deferred                                       srv*
Symbol search path is: srv*
Executable search path is:
ModLoad: 00007ff7`c9d00000 00007ff7`c9d6a000   cmd.exe
ModLoad: 00007ffc`43c30000 00007ffc`43e44000   ntdll.dll
ModLoad: 00007ffc`42540000 00007ffc`42604000   C:\Windows\System32\KERNEL32.DLL
ModLoad: 00007ffc`41310000 00007ffc`416b4000   C:\Windows\System32\KERNELBASE.dll
(6ddc.2b6c): Break instruction exception - code 80000003 (first chance)
ntdll!LdrpDoDebuggerBreak+0x30:
00007ffc`43d0b304 cc              int     3
0:000> )RAW";

/** Output of a running session: markers, a module load and a child session. */
const char* const kRunning = R"RAW(0:000> 0:000> APPBOXHIT ntdll!NtClose ntdll!ZwClose
APPBOXHIT kernel32!CreateFileW
0:000> ModLoad: 00007ffb`ebfb0000 00007ffb`ec054000   C:\Program Files\McAfee\Endpoint Security\Threat Prevention\Ips\EpMPApi.dll
ntdll!NtMapViewOfSection+0x14:
00007ffc`43ccf254 c3              ret
0:000> 1:004> APPBOXHIT ntdll!NtCreateFile
0:000> APPBOXHIT ntdll!NtOpenKey
)RAW";

/** Output which must not produce a single event. */
const char* const kNoise = R"RAW(Couldn't resolve error at 'kernel32!ActivateActCtx'
                                                     ^ addresses must be preceeded by whitespace error in 'bp /1 kernel32!ActivateActCtx'
NatVis script unloaded from 'C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\Visualizers\stl.natvis'
Breakpoint 0 hit
ntdll!RtlAllocateHeap:
00007ffc`43c6c600 48895c2408      mov     qword ptr [rsp+8],rbx ss:0000002d`e74feb90=0000021cfa649aa0
)RAW";

/**
 * @brief Feed a text to a parser and return the events.
 *
 * @param[in] text Output to decode.
 * @param[in] chunk_size Number of bytes per feed; 0 means one single feed.
 * @return The events of the parser.
 */
std::vector<appbox::tracer::CdbEvent> Decode(const std::string& text, std::size_t chunk_size = 0)
{
    appbox::tracer::CdbOutputParser parser;
    std::vector<appbox::tracer::CdbEvent> events;

    if (chunk_size == 0U)
    {
        parser.Feed(text, events);
        return events;
    }

    for (std::size_t offset = 0; offset < text.size();)
    {
        const std::size_t length = std::min(chunk_size, text.size() - offset);
        parser.Feed(std::string_view(text).substr(offset, length), events);
        offset += length;
    }

    return events;
}

/**
 * @brief Count the events of one kind.
 *
 * @param[in] events Events to inspect.
 * @param[in] kind Kind to count.
 * @return Number of events of that kind.
 */
std::size_t Count(const std::vector<appbox::tracer::CdbEvent>& events,
                  appbox::tracer::CdbEvent::Kind kind)
{
    std::size_t count = 0;
    for (const auto& event : events)
    {
        if (event.kind == kind)
        {
            ++count;
        }
    }

    return count;
}

} // namespace

/**
 * @brief A session start reports the module loads (which carry the base
 *        addresses) and the first prompt.
 */
TEST(TracerCdbOutput, ASessionStartReportsModuleLoadsAndTheFirstPrompt)
{
    const auto events = Decode(kSessionStart);

    ASSERT_EQ(Count(events, appbox::tracer::CdbEvent::Kind::ModuleLoad), 4U);
    ASSERT_EQ(Count(events, appbox::tracer::CdbEvent::Kind::Prompt), 1U);

    EXPECT_EQ(events[0].image_base, 0x00007ff7c9d00000ULL);
    EXPECT_EQ(events[0].image_path, L"cmd.exe");
    EXPECT_EQ(events[1].image_base, 0x00007ffc43c30000ULL);
    EXPECT_EQ(events[1].image_path, L"ntdll.dll");
    EXPECT_EQ(events[2].image_path, L"C:\\Windows\\System32\\KERNEL32.DLL");
    EXPECT_EQ(events[3].image_path, L"C:\\Windows\\System32\\KERNELBASE.dll");

    EXPECT_EQ(events[4].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[4].session, 0U);
}

/**
 * @brief Markers, module loads of a running session and the prompt of a child
 *        session are decoded in stream order.
 */
TEST(TracerCdbOutput, ARunningSessionIsDecodedInStreamOrder)
{
    const auto events = Decode(kRunning);

    ASSERT_EQ(events.size(), 11U);

    EXPECT_EQ(events[0].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[1].kind, appbox::tracer::CdbEvent::Kind::Prompt);

    /* The names of one address stay together, and the marker which follows the
     * prompt of a hit is decoded as well. */
    ASSERT_EQ(events[2].kind, appbox::tracer::CdbEvent::Kind::Hit);
    ASSERT_EQ(events[2].names.size(), 2U);
    EXPECT_EQ(events[2].names[0], L"ntdll!NtClose");
    EXPECT_EQ(events[2].names[1], L"ntdll!ZwClose");

    ASSERT_EQ(events[3].kind, appbox::tracer::CdbEvent::Kind::Hit);
    ASSERT_EQ(events[3].names.size(), 1U);
    EXPECT_EQ(events[3].names[0], L"kernel32!CreateFileW");

    EXPECT_EQ(events[4].kind, appbox::tracer::CdbEvent::Kind::Prompt);

    /* The module load line reports the base address which the child session
     * needs for its breakpoints. */
    ASSERT_EQ(events[5].kind, appbox::tracer::CdbEvent::Kind::ModuleLoad);
    EXPECT_EQ(events[5].image_base, 0x00007ffbebfb0000ULL);
    EXPECT_NE(events[5].image_path.find(L"EpMPApi.dll"), std::wstring::npos);

    EXPECT_EQ(events[6].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[6].session, 0U);

    /* A child process gets its own session index. */
    EXPECT_EQ(events[7].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[7].session, 1U);

    ASSERT_EQ(events[8].kind, appbox::tracer::CdbEvent::Kind::Hit);
    EXPECT_EQ(events[8].names[0], L"ntdll!NtCreateFile");

    EXPECT_EQ(events[9].kind, appbox::tracer::CdbEvent::Kind::Prompt);

    ASSERT_EQ(events[10].kind, appbox::tracer::CdbEvent::Kind::Hit);
    EXPECT_EQ(events[10].names[0], L"ntdll!NtOpenKey");
}

/**
 * @brief The decode does not depend on where the reads of the debugger output
 *        happen to split: a marker line, a module load line or a prompt may be
 *        spread over two reads.
 */
TEST(TracerCdbOutput, DecodingIsIndependentOfTheChunkBoundaries)
{
    const std::string text = std::string(kSessionStart) + kRunning;
    const auto reference = Decode(text);

    ASSERT_FALSE(reference.empty());
    for (std::size_t chunk_size = 1U; chunk_size <= 17U; ++chunk_size)
    {
        const auto chunked = Decode(text, chunk_size);
        ASSERT_EQ(chunked.size(), reference.size()) << chunk_size;
        for (std::size_t index = 0; index < reference.size(); ++index)
        {
            EXPECT_EQ(chunked[index].kind, reference[index].kind) << chunk_size << ' ' << index;
            EXPECT_EQ(chunked[index].session, reference[index].session) << chunk_size << ' ' << index;
            EXPECT_EQ(chunked[index].image_base, reference[index].image_base)
                << chunk_size << ' ' << index;
            EXPECT_EQ(chunked[index].image_path, reference[index].image_path)
                << chunk_size << ' ' << index;
            EXPECT_EQ(chunked[index].names, reference[index].names) << chunk_size << ' ' << index;
        }
    }
}

/**
 * @brief A prompt is not terminated by a newline, so it has to be reported as
 *        soon as it is complete: the session reacts to it.
 */
TEST(TracerCdbOutput, APromptIsReportedWithoutATrailingNewline)
{
    appbox::tracer::CdbOutputParser parser;
    std::vector<appbox::tracer::CdbEvent> events;

    parser.Feed("0:000", events);
    EXPECT_TRUE(events.empty());
    EXPECT_TRUE(parser.HasPendingOutput());

    parser.Feed(">", events);
    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[0].session, 0U);

    /* A marker which is split over two reads is reported once it is complete. */
    parser.Feed("\nAPPBOX", events);
    EXPECT_EQ(events.size(), 1U);
    EXPECT_TRUE(parser.HasPendingOutput());

    parser.Feed("HIT ntdll!NtClose\n", events);
    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[1].kind, appbox::tracer::CdbEvent::Kind::Hit);
    ASSERT_EQ(events[1].names.size(), 1U);
    EXPECT_EQ(events[1].names[0], L"ntdll!NtClose");
}

/**
 * @brief A prompt of a WOW64 process carries the architecture, and a thread id
 *        of four digits is accepted as well.
 */
TEST(TracerCdbOutput, Wow64AndLongThreadIdsAreAccepted)
{
    const auto events = Decode("0:000:x86> 0:2ba8> ");

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].kind, appbox::tracer::CdbEvent::Kind::Prompt);
    EXPECT_EQ(events[0].session, 0U);
    EXPECT_EQ(events[1].session, 0U);
}

/**
 * @brief Startup noise, error lines and the messages of the shutdown produce no
 *        event at all.
 */
TEST(TracerCdbOutput, NoiseProducesNoEvents)
{
    const auto events = Decode(kNoise);

    EXPECT_TRUE(events.empty());
}

/**
 * @brief A garbled marker line must not put a wrong name into the report.
 */
TEST(TracerCdbOutput, GarbledMarkersAreIgnored)
{
    const auto events = Decode("APPBOXHIT kernel32!CreateFileW\"; g\"\n"
                               "APPBOXHIT g\"\"\n"
                               "APPBOXHIT notaname\n"
                               "APPBOXHIT ntdll!NtClose\n");

    ASSERT_EQ(events.size(), 1U);
    ASSERT_EQ(events[0].names.size(), 1U);
    EXPECT_EQ(events[0].names[0], L"ntdll!NtClose");
}

/**
 * @brief A module load line which does not carry two addresses is ignored
 *        instead of reporting a wrong base address.
 */
TEST(TracerCdbOutput, IncompleteModuleLoadLinesAreIgnored)
{
    EXPECT_TRUE(Decode("ModLoad: 00007ffc`42540000   C:\\Windows\\System32\\KERNEL32.DLL\n").empty());
    EXPECT_TRUE(Decode("ModLoad:\n").empty());
}

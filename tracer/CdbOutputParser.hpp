#ifndef APPBOX_TRACER_CDBOUTPUTPARSER_HPP
#define APPBOX_TRACER_CDBOUTPUTPARSER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace appbox::tracer
{

/** One decoded event of the debugger output stream. */
struct CdbEvent
{
    /** What the debugger reported. */
    enum class Kind
    {
        Prompt,     ///< The debugger is ready for input; `session` is the process index.
        ModuleLoad, ///< A module was mapped; `image_path` and `image_base` describe it.
        Hit,        ///< An armed address was entered; `names` are the names of that address.
    };

    Kind kind = Kind::Prompt;         ///< Kind of the event.
    std::uint32_t session = 0;        ///< Process index of a prompt.
    std::wstring image_path;          ///< Image path of a module load.
    std::uint64_t image_base = 0;     ///< Base address of a module load.
    std::vector<std::wstring> names;  ///< Function names of a hit.
};

/**
 * @brief Incremental decoder of the debugger output stream.
 *
 * The parser turns the raw bytes of cdb into the events the session reacts to.
 * It is incremental because the output arrives in arbitrary chunks: a marker
 * line, a module load line or a prompt can be split across two reads, and a
 * prompt is not even terminated by a newline (it is the last thing before the
 * debugger waits for input, so it must be reported without waiting for a
 * newline).
 *
 * Everything else is ignored on purpose: the startup banner, the symbol path
 * report, the hardware stack protection notice, breakpoint hit messages of
 * breakpoints without a command, error lines and the NatVis messages of the
 * shutdown.
 */
class CdbOutputParser
{
public:
    /**
     * @brief Consume a chunk of the debugger output.
     *
     * @param[in] chunk Raw bytes as they were read from the debugger.
     * @param[in,out] events Events which are appended in stream order.
     */
    void Feed(std::string_view chunk, std::vector<CdbEvent>& events);

    /** @return Whether a partial line is buffered. */
    bool HasPendingOutput() const noexcept { return !pending_.empty(); }

private:
    std::string pending_; ///< Bytes of the output which are not decoded yet.
};

} // namespace appbox::tracer

#endif // APPBOX_TRACER_CDBOUTPUTPARSER_HPP

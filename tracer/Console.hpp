#ifndef APPBOX_TRACER_CONSOLE_HPP
#define APPBOX_TRACER_CONSOLE_HPP

#include <string>

namespace appbox::tracer
{

/**
 * @brief Write text to the standard output.
 *
 * A console receives UTF-16 through WriteConsoleW, so the text is shown with
 * the code page independent wide character API; a redirected stream receives
 * UTF-8 bytes instead.
 *
 * @param[in] text Text to write.
 */
void WriteStdout(const std::wstring& text);

/**
 * @brief Write text to the standard error, with the same encoding rules as
 *        WriteStdout.
 *
 * @param[in] text Text to write.
 */
void WriteStderr(const std::wstring& text);

/**
 * @brief Decode bytes which a console program wrote to a redirected stream.
 *
 * The debugger writes its output in the code page of the console, which is the
 * ANSI code page when the stream is a pipe. The raw output of a run is decoded
 * with this function before it is stored as UTF-8.
 *
 * @param[in] bytes Bytes as they were read.
 * @return The decoded text.
 */
std::wstring DecodeConsoleBytes(const std::string& bytes);

} // namespace appbox::tracer

#endif // APPBOX_TRACER_CONSOLE_HPP

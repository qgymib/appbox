#include "BuildReport.hpp"
#include <cstdio>

namespace appbox
{

std::string FormatElapsed(std::chrono::milliseconds elapsed)
{
    if (elapsed.count() < 0)
    {
        elapsed = std::chrono::milliseconds::zero();
    }

    const auto seconds =
        static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count());
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto rest = seconds % 60;

    char buffer[32] = {};
    if (hours > 0)
    {
        std::snprintf(buffer, sizeof(buffer), "%llu:%02llu:%02llu", hours, minutes, rest);
    }
    else
    {
        std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu", minutes, rest);
    }

    return std::string(buffer);
}

} // namespace appbox

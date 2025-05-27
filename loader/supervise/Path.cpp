#include "Path.hpp"

std::string appbox::GetPathInSandbox(const std::string& sandboxLocation, const std::string& path)
{
    std::string copyPath = path;
    copyPath.erase(std::remove(copyPath.begin(), copyPath.end(), ':'), copyPath.end());
    return sandboxLocation + "\\" + copyPath;
}

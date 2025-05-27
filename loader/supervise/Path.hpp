#ifndef APPBOX_LOADER_SUPERVISE_PATH_HPP
#define APPBOX_LOADER_SUPERVISE_PATH_HPP

#include <string>

namespace appbox
{

std::string GetPathInSandbox(const std::string& sandboxLocation, const std::string& path);

}

#endif

#ifndef APPBOX_LOADER_SUPERVISE_INIT_HPP
#define APPBOX_LOADER_SUPERVISE_INIT_HPP


#include "VariableDecoder.hpp"

namespace appbox
{

namespace supervise
{

void Init();
void Exit();

VariableDecoder& GetVariableDecoder();

} // namespace supervise

} // namespace appbox

#endif

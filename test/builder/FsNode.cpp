#include "FsNode.hpp"

appbox::test::FsNode::FsNode(const std::wstring& name)
{
    this->name = name;
}

const std::wstring& appbox::test::FsNode::GetName() const
{
    return this->name;
}

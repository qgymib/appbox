#ifndef APPBOX_VARIABLE_DECODER_HPP
#define APPBOX_VARIABLE_DECODER_HPP

#include <string>
#include <vector>

class VariableDecoder
{
public:
    struct Variable
    {
        std::wstring key;
        std::wstring value;
    };

    VariableDecoder();
    ~VariableDecoder();

    void Append(const Variable& var);
    void Append(const std::wstring& key, const std::wstring& value);
    void Append(const Variable* vars, size_t size);
    std::wstring Decode(const std::wstring& str) const;

private:
    struct Data;
    Data* mData;
};

#endif

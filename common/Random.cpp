#include <random>
#include "Random.hpp"

std::string appbox::RandomString(size_t length)
{
    static const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::random_device                    rd; // 真随机种子
    std::mt19937                          gen(rd());
    std::uniform_int_distribution<size_t> dis(0, std::size(chars) - 2);

    std::string result;
    for (size_t i = 0; i < length; ++i)
    {
        result += chars[dis(gen)];
    }

    return result;
}

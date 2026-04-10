#include <spdlog/spdlog.h>
#include <sstream>
#include <chrono>
#include <random>
#include "rpc/__init__.hpp"
#include "Loader.hpp"

extern "C" {
extern uint8_t dll_sandbox_x86_data[];
extern size_t  dll_sandbox_x86_size;
extern uint8_t dll_sandbox_x64_data[];
extern size_t  dll_sandbox_x64_size;
}

appbox::Loader* appbox::loader = nullptr;

std::string GenerateRandomString(size_t length)
{
    const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    std::random_device                    rd; // 真随机种子
    std::mt19937                          gen(rd());
    std::uniform_int_distribution<size_t> dis(0, std::size(chars) - 1);

    std::string result;
    for (size_t i = 0; i < length; ++i)
    {
        result += chars[dis(gen)];
    }

    return result;
}

appbox::Loader::Loader()
{
    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = GenerateRandomString(16);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    this->inject_data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    this->temp_dir = std::filesystem::temp_directory_path() / unique_path;

    this->pipe_server = RemoteServer::Create(this->inject_data.pipe_path);
    std::filesystem::create_directory(this->temp_dir);

    this->inject_data.sandbox32_path = (this->temp_dir / "sandbox32.dll").string();
    {
        std::ofstream ofs(this->inject_data.sandbox32_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(dll_sandbox_x86_data),
                  static_cast<std::streamsize>(dll_sandbox_x86_size));
    }

    this->inject_data.sandbox64_path = (this->temp_dir / "sandbox64.dll").string();
    {
        std::ofstream ofs(this->inject_data.sandbox64_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(dll_sandbox_x64_data),
                  static_cast<std::streamsize>(dll_sandbox_x64_size));
    }
}

appbox::Loader::~Loader()
{
    this->pipe_server.reset();
    std::filesystem::remove_all(this->temp_dir);
}

void appbox::InitLoader()
{
    if (loader != nullptr)
    {
        return;
    }
    loader = new Loader();

    /* Start pipe server */
    loader->pipe_server = RemoteServer::Create(loader->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit();
    loader->pipe_server->Start();
}

void appbox::ExitLoader()
{
    delete loader;
    loader = nullptr;
}

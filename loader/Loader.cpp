#include <spdlog/spdlog.h>
#include <memory>
#include <sstream>
#include <chrono>
#include <random>
#include "rpc/__init__.hpp"
#include "WString.hpp"
#include "Loader.hpp"

extern "C" {
extern uint8_t dll_sandbox_x86_data[];
extern size_t  dll_sandbox_x86_size;
extern uint8_t dll_sandbox_x64_data[];
extern size_t  dll_sandbox_x64_size;
}

appbox::Loader::Ptr appbox::loader;

static std::string GenerateRandomString(size_t length)
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

appbox::Loader::~Loader()
{
    this->pipe_server.reset();
    std::filesystem::remove_all(this->temp_dir);
}

static appbox::InjectData GenerateInjectData(const std::filesystem::path& tmp_path,
                                             const std::string&           unique_path,
                                             const std::wstring&          sandbox_path)
{
    appbox::InjectData data;
    data.pipe_path = fmt::format(R"(\\.\pipe\{})", unique_path);
    data.sandbox_path = appbox::WideToUTF8(sandbox_path.c_str());
    data.sandbox32_dll_path = (tmp_path / "sandbox32.dll").string();
    data.sandbox64_dll_path = (tmp_path / "sandbox64.dll").string();

    return data;
}

appbox::Loader::Ptr appbox::Loader::Create(const std::wstring& sandbox_path)
{
    auto obj = std::make_shared<Loader>();

    std::time_t timestamp = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    auto        random_str = GenerateRandomString(8);
    auto        unique_path = fmt::format("appbox-{}-{}", timestamp, random_str);

    obj->temp_dir = std::filesystem::path(sandbox_path) / unique_path;
    std::filesystem::create_directory(obj->temp_dir);

    obj->inject_data = GenerateInjectData(obj->temp_dir, unique_path, sandbox_path);
    obj->pipe_server = RemoteServer::Create(obj->inject_data.pipe_path);

    {
        std::ofstream ofs(obj->inject_data.sandbox32_dll_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(dll_sandbox_x86_data),
                  static_cast<std::streamsize>(dll_sandbox_x86_size));
    }

    {
        std::ofstream ofs(obj->inject_data.sandbox64_dll_path, std::ios::binary);
        ofs.write(reinterpret_cast<const char*>(dll_sandbox_x64_data),
                  static_cast<std::streamsize>(dll_sandbox_x64_size));
    }

    /* Start pipe server */
    obj->pipe_server = RemoteServer::Create(obj->inject_data.pipe_path);

    /* Methods must register before rpc server start. */
    appbox::RpcInit(obj->pipe_server);
    obj->pipe_server->Start();

    return obj;
}

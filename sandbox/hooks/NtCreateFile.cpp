#include "__init__.hpp"

static void s_init_NtCreateFile()
{
}

const appbox::Detour appbox::NtCreateFile = {
    L"NtCreateFile",
    s_init_NtCreateFile,
};

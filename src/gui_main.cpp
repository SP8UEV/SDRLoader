// gui_main.cpp -- sdrloader.exe, the windowed program.
//
//   --folder <path>   also look in this folder (repeatable)
//   --elevated        set by the program itself when it restarts as administrator
#include "gui.hpp"
#include "i18n.hpp"

#include <shellapi.h>

#include <string>
#include <vector>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    sdi::detectLanguage();
    std::vector<std::wstring> folders;
    bool elevated = false;
    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            const std::wstring a = argv[i];
            if (a == L"--folder" && i + 1 < argc)
                folders.push_back(argv[++i]);
            else if (a == L"--elevated")
                elevated = true;
        }
        LocalFree(argv);
    }
    return sdi::runGui(inst, folders, elevated);
}

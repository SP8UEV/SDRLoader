// gui.hpp -- the window.
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace sdi {

int runGui(HINSTANCE inst, const std::vector<std::wstring>& extraFolders, bool elevatedRestart);

}  // namespace sdi

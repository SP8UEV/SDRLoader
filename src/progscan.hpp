// progscan.hpp -- which SDR programs are on this computer, and what rtlsdr.dll they have.
//
// Read-only and local: it looks at file NAMES only, and nothing it finds
// leaves the computer. The standard scan looks where programs are normally
// found -- the installed-programs list in the registry, Program Files, the
// Start menu and desktop shortcuts, and the usual places a portable program is
// unzipped to (Desktop, Downloads, Documents, folders at the root of a drive).
// The deep scan, which walks whole drives, runs only when the user asks for it.
#pragma once

#include "catalog.hpp"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

namespace sdi {

struct DllInfo {
    std::wstring path;
    Arch arch = Arch::Unknown;
    uint64_t size = 0;
};

struct ProgramFinding {
    const ProgramProfile* profile = nullptr;
    std::wstring exePath;  // empty when found by its data folder only
    std::wstring dir;      // where rtlsdr.dll goes: the .exe's folder, or the profile's fixed folder
    Arch arch = Arch::Unknown;
    std::vector<DllInfo> rtlDlls;  // rtlsdr.dll / librtlsdr.dll already in `dir`
    std::wstring shadowDll;        // an rtlsdr.dll beside the .exe that the program loads before `dir`
    bool siblingOk = true;         // the profile's requireSibling is satisfied
    std::wstring foundBy;
};

struct ProgramScanOptions {
    bool standard = true;
    bool deep = false;
    std::vector<std::wstring> extraFolders;
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(const std::wstring&)> status;  // "now looking in ..."
};

struct ProgramScanResult {
    std::vector<ProgramFinding> programs;
    std::vector<DllInfo> looseDlls;  // rtlsdr.dll found outside every program found
    uint64_t foldersVisited = 0;
    bool cancelled = false;
};

ProgramScanResult scanPrograms(const ProgramScanOptions& o);

// What is known about one program, from its profile and .exe (used by the scan and the tests).
ProgramFinding describeProgram(const ProgramProfile& p, const std::wstring& exePath, const std::wstring& foundBy);
std::vector<DllInfo> rtlDllsIn(const std::wstring& dir);

}  // namespace sdi

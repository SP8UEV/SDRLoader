// cli_main.cpp -- sdrloader-cli.exe: the same scan as text, for support and scripts.
//
//   --scan [--deep] [--folder <path>]...  what is plugged in, what is installed, what would be proposed
//   --list-payload                         the files an offline package may hold (tab-separated)
//   --lang en|pl   --version   --help
//
// It installs nothing and changes nothing on the system; the only thing it may
// write is this program's own cache in %LOCALAPPDATA%\SDRLoader.
#include "actions.hpp"
#include "catalog.hpp"
#include "planner.hpp"
#include "progscan.hpp"
#include "usbscan.hpp"
#include "version.hpp"

#include <windows.h>

#include <string>
#include <vector>

namespace {

using namespace sdi;

void out(const std::wstring& s) {
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0, n = 0;
    if (GetConsoleMode(h, &mode)) {
        const std::wstring line = s + L"\n";
        WriteConsoleW(h, line.c_str(), static_cast<DWORD>(line.size()), &n, nullptr);
    } else {
        const std::string u = narrow(s + L"\r\n");  // a file or a pipe: UTF-8
        WriteFile(h, u.data(), static_cast<DWORD>(u.size()), &n, nullptr);
    }
}

std::wstring dlls(const std::vector<DllInfo>& v) {
    if (v.empty()) return L"-";
    std::wstring s;
    for (const DllInfo& d : v) {
        if (!s.empty()) s += L"; ";
        s += fileName(d.path) + L" " + archName(d.arch) + L" " + formatBytes(d.size);
    }
    return s;
}

int scan(bool deep, const std::vector<std::wstring>& folders) {
    out(std::wstring(SDI_PRODUCT_W L" " SDI_VERSION_W L" -- ") +
        L"scan (nothing is changed)");
    out(L"");

    const std::vector<DeviceFinding> devices = evaluateDevices(enumerateUsb());
    out(L"SDR radios plugged in:");
    if (devices.empty()) out(std::wstring(L"  ") + L"(none recognised)");
    for (const DeviceFinding& d : devices) {
        out(L"  " + std::wstring(d.kind->name) + L"  [" + statusText(d.status) + L"]  " +
            L"driver: " + (d.service.empty() ? std::wstring(L"-") : d.service));
        out(L"      " + adviceFor(d));
        out(L"      " + d.instanceId);
    }
    out(L"");

    ProgramScanOptions o;
    o.deep = deep;
    o.extraFolders = folders;
    const ProgramScanResult progs = scanPrograms(o);
    out(L"SDR programs:");
    if (progs.programs.empty()) out(std::wstring(L"  ") + L"(none found)");
    for (const ProgramFinding& f : progs.programs) {
        std::wstring rtl = dlls(f.rtlDlls);
        if (f.profile->rtl == RtlPolicy::ShipsOwn) rtl = L"its own";
        if (f.profile->rtl == RtlPolicy::None) rtl = L"not used";
        if (f.profile->requireSibling && !f.siblingOk) rtl += L" (no ExtIO for RTL here)";
        if (!f.shadowDll.empty()) rtl = f.shadowDll + L" (loaded first)";
        out(L"  " + std::wstring(f.profile->name) + L"  [" + archName(f.arch) + L"]  " + f.dir);
        out(L"      rtlsdr.dll: " + rtl + L"   (" + L"found by: " + f.foundBy + L")");
        if (!f.exePath.empty()) out(L"      " + f.exePath);
    }
    if (!progs.looseDlls.empty()) {
        out(L"Other rtlsdr.dll files:");
        for (const DllInfo& d : progs.looseDlls)
            out(L"  " + d.path + L"  [" + archName(d.arch) + L", " + formatBytes(d.size) + L"]");
    }
    out(std::wstring(L"Folders looked in: ") + std::to_wstring(progs.foldersVisited));
    out(L"");

    const SystemState sys = probeSystem();
    out(L"System:");
    out(std::wstring(L"  Visual C++ runtime x64: ") + (sys.vcrtX64 ? L"yes" : L"no") +
        L", x86: " + (sys.vcrtX86 ? L"yes" : L"no"));
    out(std::wstring(L"  SDRplay API: ") +
        (sys.sdrplayApi ? sys.sdrplayApiDir : std::wstring(L"not installed")));
    out(L"");

    ActionContext c = defaultContext();
    const RtlReference ref = rtlReference(c);
    const std::vector<PlanItem> plan = makePlan(devices, progs.programs, sys, ref);
    out(L"Proposed actions ([x] = ticked by default):");
    if (plan.empty()) out(std::wstring(L"  ") + L"(nothing to do)");
    for (const PlanItem& p : plan) {
        out(std::wstring(L"  [") + (p.checked ? L"x" : L" ") + L"] " + p.title);
        out(L"      " + p.reason);
    }
    return 0;
}

int listPayload() {
    for (const Package& p : packages()) {
        if (p.payload == Payload::Never) continue;
        out(std::wstring(p.id) + L"\t" + p.file + L"\t" + p.url + L"\t" + std::to_wstring(p.size) + L"\t" +
            (p.sha256 ? widen(p.sha256) : std::wstring()) + L"\t" + (p.signer ? p.signer : L"") + L"\t" +
            payloadName(p.payload) + L"\t" + (p.sourceArchive ? p.sourceArchive : L""));
    }
    return 0;
}

void help() {
    out(SDI_PRODUCT_W L" " SDI_VERSION_W);
    out(L"Usage:");
    out(L"  sdrloader-cli --scan [--deep] [--folder <path>]...   show what is found and proposed");
    out(L"  sdrloader-cli --list-payload                          files an offline package may hold");
    out(L"  --version   --help");
    out(L"Installing is done in the window: sdrloader.exe");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    bool doScan = false, deep = false, payload = false;
    std::vector<std::wstring> folders;
    for (int i = 1; i < argc; ++i) {
        const std::wstring a = argv[i];
        if (a == L"--scan")
            doScan = true;
        else if (a == L"--deep")
            deep = doScan = true;
        else if (a == L"--folder" && i + 1 < argc)
            folders.push_back(argv[++i]), doScan = true;
        else if (a == L"--list-payload")
            payload = true;
        else if (a == L"--version") {
            out(SDI_VERSION_W);
            return 0;
        } else {
            help();
            return a == L"--help" || a == L"-h" || a == L"/?" ? 0 : 2;
        }
    }
    if (payload) return listPayload();
    if (doScan) return scan(deep, folders);
    help();
    return 0;
}

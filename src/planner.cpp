#include "planner.hpp"

#include "driverinstall.hpp"
#include "i18n.hpp"

#include <windows.h>

#include <algorithm>

#ifndef RRF_SUBKEY_WOW6464KEY
#define RRF_SUBKEY_WOW6464KEY 0x00010000
#endif
#ifndef RRF_SUBKEY_WOW6432KEY
#define RRF_SUBKEY_WOW6432KEY 0x00020000
#endif

namespace sdi {

SystemState probeSystem() {
    SystemState s;
    const std::wstring wow = systemWow64Dir();
    if (wow.empty()) {  // 32-bit Windows: System32 holds the 32-bit runtime
        s.vcrtX86 = fileExists(joinPath(systemDir(), L"vcruntime140.dll"));
    } else {
        s.vcrtX64 = fileExists(joinPath(systemDir(), L"vcruntime140.dll"));
        s.vcrtX86 = fileExists(joinPath(wow, L"vcruntime140.dll"));
    }
    // Where SDRplay's installer records the API (SDRplay API specification, section 6).
    for (DWORD view : {static_cast<DWORD>(RRF_SUBKEY_WOW6464KEY), static_cast<DWORD>(RRF_SUBKEY_WOW6432KEY)}) {
        wchar_t dir[MAX_PATH] = {};
        DWORD bytes = sizeof dir - sizeof(wchar_t);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\SDRplay\\Service\\API", L"Install_Dir", RRF_RT_REG_SZ | view,
                         nullptr, dir, &bytes) == ERROR_SUCCESS &&
            dir[0] && dirExists(dir)) {
            s.sdrplayApi = true;
            s.sdrplayApiDir = dir;
            break;
        }
    }
    return s;
}

std::wstring winUsbGuid(uint16_t vid, uint16_t pid, int mi) {
    // {6B3F2E31-VVVV-PPPP-MMMM-534452494E53} -- fixed prefix, VID/PID/interface,
    // "SDRINS" in the node; distinct per radio type, stable across installs.
    return L"{6B3F2E31-" + hex4(vid) + L"-" + hex4(pid) + L"-" + hex4(static_cast<uint16_t>(mi < 0 ? 0xFFFF : mi)) +
           L"-534452494E53}";
}

std::wstring packageActionTitle(const Package& p) {
    switch (p.kind) {
        case PkgKind::VendorInstaller: return std::wstring(tr(L"Run the installer: ", L"Uruchom instalator: ")) + p.name;
        case PkgKind::Tool: return std::wstring(tr(L"Start ", L"Uruchom ")) + p.name;
        case PkgKind::WebPage: return std::wstring(tr(L"Open the download page: ", L"Otwórz stronę pobierania: ")) + p.name;
        case PkgKind::Archive: return p.name;
    }
    return p.name;
}

namespace {

ActionType actionFor(const Package& p) {
    switch (p.kind) {
        case PkgKind::Tool: return ActionType::RunTool;
        case PkgKind::WebPage: return ActionType::OpenPage;
        default: return ActionType::RunInstaller;
    }
}

int rank(const PlanItem& i) {
    switch (i.type) {
        case ActionType::InstallWinUsb: return 0;
        case ActionType::RunInstaller: return i.packageId.compare(0, 9, L"vcredist-") == 0 ? 3 : 1;
        case ActionType::RunTool: return 2;
        case ActionType::CopyRtlSdr: return 4;
        case ActionType::OpenPage: return 5;
    }
    return 6;
}

}  // namespace

std::vector<PlanItem> makePlan(const std::vector<DeviceFinding>& devices, const std::vector<ProgramFinding>& programs,
                               const SystemState& sys, const RtlReference& ref) {
    std::vector<PlanItem> plan;
    // One item per key; a second reason for the same action is appended to the first.
    auto add = [&plan](const PlanItem& it) {
        for (PlanItem& p : plan)
            if (p.key == it.key) {
                if (!it.reason.empty() && p.reason.find(it.reason) == std::wstring::npos) p.reason += L" " + it.reason;
                p.checked = p.checked || it.checked;
                return;
            }
        plan.push_back(it);
    };
    auto packageItem = [](const Package& p, bool checked, const std::wstring& reason) {
        PlanItem it;
        it.type = actionFor(p);
        it.packageId = p.id;
        it.key = std::wstring(L"pkg:") + p.id;
        it.checked = checked;
        it.title = packageActionTitle(p);
        it.reason = reason;
        return it;
    };

    bool rtlPresent = false, sdrplayPresent = false;
    for (const DeviceFinding& d : devices) {
        const std::wstring id = d.kind->id;
        if (id == L"rtlsdr") rtlPresent = true;
        if (id == L"sdrplay") sdrplayPresent = true;
        if (d.status != DevStatus::NoDriver && d.status != DevStatus::WrongDriver) continue;
        std::wstring why = std::wstring(d.kind->name) + L": ";
        why += d.status == DevStatus::NoDriver
                   ? tr(L"no driver.", L"brak sterownika.")
                   : std::wstring(tr(L"wrong driver (", L"zły sterownik (")) + d.service + L").";

        // A radio that needs WinUSB: install it automatically where this build
        // can (libwdi), and fall back to Zadig otherwise.
        if (d.kind->rule == DriverRule::NeedsWinUsb && driverinst::available()) {
            PlanItem it;
            it.type = ActionType::InstallWinUsb;
            it.vid = d.targetVid;
            it.pid = d.targetPid;
            it.mi = d.targetMi;
            it.interfaceGuid = winUsbGuid(d.targetVid, d.targetPid, d.targetMi);
            it.deviceDesc = d.description.empty() ? std::wstring(d.kind->name) : d.description;
            it.needsAdmin = true;
            it.checked = true;
            it.key = L"winusb:" + hex4(d.targetVid) + L"-" + hex4(d.targetPid) + L"-" +
                     std::to_wstring(d.targetMi);
            it.title = std::wstring(tr(L"Install the WinUSB driver (automatic) for ",
                                       L"Zainstaluj sterownik WinUSB (automatycznie) dla ")) +
                       d.kind->name;
            it.reason = why + tr(L" The driver is installed for you -- no Zadig window. Needs administrator "
                                 L"rights and your confirmation; it adds a driver certificate (see the prompt).",
                                 L" Sterownik zostanie zainstalowany za Ciebie -- bez okna Zadiga. Wymaga "
                                 L"uprawnień administratora i Twojego potwierdzenia; dodaje certyfikat sterownika "
                                 L"(zobacz pytanie).");
            add(it);
            continue;
        }
        const Package* p = d.kind->fixPackage ? findPackage(d.kind->fixPackage) : nullptr;
        if (!p) continue;
        add(packageItem(*p, true, why));
    }
    if (sdrplayPresent && !sys.sdrplayApi)
        if (const Package* p = findPackage(L"sdrplay-api"))
            add(packageItem(*p, true,
                            tr(L"An SDRplay RSP is plugged in and the SDRplay API is not installed.",
                               L"Podłączono SDRplay RSP, a SDRplay API nie jest zainstalowane.")));

    for (const ProgramFinding& f : programs) {
        const ProgramProfile& prof = *f.profile;
        if (prof.needsPackage && !sys.sdrplayApi && std::wstring(prof.needsPackage) == L"sdrplay-api")
            if (const Package* p = findPackage(prof.needsPackage))
                add(packageItem(*p, false,
                                std::wstring(prof.name) +
                                    tr(L" uses the SDRplay API, which is not installed.",
                                       L" używa SDRplay API, które nie jest zainstalowane.")));

        if (prof.rtl != RtlPolicy::ExeDir && prof.rtl != RtlPolicy::FixedDir) continue;
        if (f.dir.empty() || !f.siblingOk) continue;
        if (!f.shadowDll.empty()) continue;  // it loads another rtlsdr.dll first: a copy in `dir` would go unused
        const DllInfo* existing = nullptr;
        for (const DllInfo& d : f.rtlDlls)
            if (iequals(fileName(d.path), L"rtlsdr.dll")) existing = &d;
        // The build a program already loads tells which one it needs; failing that, its own .exe.
        Arch a = prof.rtl == RtlPolicy::FixedDir ? prof.fixedArch
                                                 : (existing && existing->arch != Arch::Unknown ? existing->arch : f.arch);
        if (a != Arch::X86 && a != Arch::X64) continue;  // the package has no ARM64 build
        const std::string& refHash = a == Arch::X64 ? ref.x64 : ref.x86;
        if (existing && !refHash.empty()) {
            std::string h;
            if (sha256File(existing->path, h) && h == refHash) continue;  // already exactly this file
        }

        PlanItem it;
        it.type = ActionType::CopyRtlSdr;
        it.packageId = L"rtlsdr-blog";
        it.targetDir = f.dir;
        it.programName = prof.name;
        it.arch = a;
        it.replaces = existing != nullptr;
        it.checked = rtlPresent && !it.replaces;
        it.key = L"copy:" + lower(f.dir);
        it.title = std::wstring(tr(L"Put rtlsdr.dll (", L"Wgraj rtlsdr.dll (")) + archName(a) +
                   tr(L", RTL-SDR Blog V1.4.0) for ", L", RTL-SDR Blog V1.4.0) dla ") + prof.name + L": " + f.dir;
        it.reason = it.replaces ? tr(L"An rtlsdr.dll is already there: it is backed up first and can be restored.",
                                     L"Jest już tam rtlsdr.dll: najpierw zostanie zachowana kopia, którą można przywrócić.")
                                : tr(L"There is no rtlsdr.dll there yet.", L"Nie ma tam jeszcze rtlsdr.dll.");
        if (!rtlPresent) it.reason += tr(L" (No RTL-SDR is plugged in right now.)", L" (Teraz nie podłączono RTL-SDR.)");

        const bool vc = a == Arch::X64 ? sys.vcrtX64 : sys.vcrtX86;
        if (!vc)
            if (const Package* p = findPackage(a == Arch::X64 ? L"vcredist-x64" : L"vcredist-x86")) {
                PlanItem v = packageItem(*p, it.checked,
                                         tr(L"rtlsdr.dll (RTL-SDR Blog build) needs Microsoft's Visual C++ runtime "
                                            L"(VCRUNTIME140.dll), which is not installed.",
                                            L"rtlsdr.dll (wersja RTL-SDR Blog) potrzebuje środowiska Microsoft Visual "
                                            L"C++ (VCRUNTIME140.dll), którego nie ma."));
                it.linkedKey = v.key;
                add(v);
            }
        add(it);
    }
    std::stable_sort(plan.begin(), plan.end(), [](const PlanItem& a, const PlanItem& b) { return rank(a) < rank(b); });
    return plan;
}

}  // namespace sdi

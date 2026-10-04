// test_core.cpp -- the parts that decide things, checked without a radio or a network.
#include "../src/actions.hpp"
#include "../src/catalog.hpp"
#include "../src/driverinstall.hpp"
#include "../src/i18n.hpp"
#include "../src/planner.hpp"
#include "../src/progscan.hpp"
#include "../src/usbscan.hpp"
#include "../src/util.hpp"

#include <windows.h>

#include <cstdio>
#include <set>
#include <string>

using namespace sdi;

static int g_failed = 0, g_passed = 0;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (cond) {                                                       \
            ++g_passed;                                                   \
        } else {                                                          \
            ++g_failed;                                                   \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
        }                                                                 \
    } while (0)

static std::wstring tempDir(const wchar_t* name) {
    wchar_t t[MAX_PATH];
    GetTempPathW(MAX_PATH, t);
    std::wstring d = joinPath(t, std::wstring(L"sdi-test-") + name + L"-" + std::to_wstring(GetCurrentProcessId()));
    if (dirExists(d)) deleteTree(d);
    ensureDir(d);
    return d;
}

static void testStrings() {
    CHECK(wildcardMatch(L"ExtIO*RTL*.dll", L"ExtIO_RTL2832.dll"));
    CHECK(wildcardMatch(L"ExtIO*RTL*.dll", L"extio_rtl.DLL"));
    CHECK(!wildcardMatch(L"ExtIO*RTL*.dll", L"ExtIO_HackRF.dll"));
    CHECK(wildcardMatch(L"*", L""));
    CHECK(wildcardMatch(L"a?c", L"abc"));
    CHECK(!wildcardMatch(L"a?c", L"ac"));
    CHECK(iequals(L"WinUSB", L"WINUSB"));
    CHECK(istartsWith(L"https://example.org", L"HTTPS://"));
    CHECK(trim(L"  x y \t") == L"x y");
    CHECK(hex4(0x0bda) == L"0BDA");
    CHECK(parentDir(L"C:\\a\\b.exe") == L"C:\\a");
    CHECK(parentDir(L"C:\\b.exe") == L"C:\\");
    CHECK(fileName(L"C:\\a\\b.exe") == L"b.exe");
    CHECK(narrow(widen("zażółć")) == "zażółć");
    CHECK(quoteArg(L"plain") == L"plain");
    CHECK(quoteArg(L"with space") == L"\"with space\"");
    CHECK(quoteArg(L"C:\\dir with space\\") == L"\"C:\\dir with space\\\\\"");
}

static void testSha256() {
    CHECK(sha256Hex("", 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(sha256Hex("abc", 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::wstring d = tempDir(L"sha");
    const std::wstring f = joinPath(d, L"abc.txt");
    CHECK(writeFile(f, "abc"));
    std::string h;
    CHECK(sha256File(f, h) && h == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    deleteTree(d);
}

static void testPe() {
    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, &self[0], static_cast<DWORD>(self.size())));
    CHECK(peArch(self) == Arch::X64);
    const auto imports = peImports(self);
    bool kernel = false;
    for (const std::wstring& i : imports)
        if (iequals(i, L"KERNEL32.dll")) kernel = true;
    CHECK(kernel);
    const std::wstring wow = systemWow64Dir();
    if (!wow.empty()) CHECK(peArch(joinPath(wow, L"kernel32.dll")) == Arch::X86);
    CHECK(peArch(joinPath(systemDir(), L"kernel32.dll")) == Arch::X64);
    // A file that is not a PE file at all.
    const std::wstring d = tempDir(L"pe");
    const std::wstring junk = joinPath(d, L"junk.dll");
    writeFile(junk, "MZ this is not really a program");
    CHECK(peArch(junk) == Arch::Unknown);
    CHECK(peImports(junk).empty());
    deleteTree(d);
}

static void testSignature() {
    // Windows' own files are catalog-signed; notepad.exe is not always embedded-signed, so use
    // a file that certainly carries no signature instead and expect a refusal.
    const std::wstring d = tempDir(L"sig");
    const std::wstring f = joinPath(d, L"unsigned.exe");
    writeFile(f, "MZ");
    std::wstring who, err;
    CHECK(!authenticodeSigner(f, who, err));
    CHECK(!err.empty());
    deleteTree(d);
}

static void testUsbIds() {
    uint16_t v = 0, p = 0;
    int mi = 0;
    CHECK(parseUsbId(L"USB\\VID_0BDA&PID_2838&MI_00\\7&1B2C3D4E&0&0000", v, p, mi));
    CHECK(v == 0x0BDA && p == 0x2838 && mi == 0);
    CHECK(parseUsbId(L"USB\\VID_1d50&PID_6089\\0000000000000000457863c82b2f2c5f", v, p, mi));
    CHECK(v == 0x1D50 && p == 0x6089 && mi == -1);
    CHECK(!parseUsbId(L"USB\\ROOT_HUB30\\4&2A8B1F&0&0", v, p, mi));
    CHECK(matchDevice(0x0BDA, 0x2838) && std::wstring(matchDevice(0x0BDA, 0x2838)->id) == L"rtlsdr");
    CHECK(matchDevice(0x1DF7, 0x3050) && std::wstring(matchDevice(0x1DF7, 0x3050)->id) == L"sdrplay");
    CHECK(matchDevice(0x0BDA, 0x8153) == nullptr);  // a Realtek network adapter is not a radio
}

static UsbNode node(const wchar_t* id, const wchar_t* parent, const wchar_t* service, unsigned long problem = 0) {
    UsbNode n;
    n.instanceId = id;
    n.parentId = parent;
    n.service = service;
    n.problem = problem;
    parseUsbId(n.instanceId, n.vid, n.pid, n.mi);
    return n;
}

static void testEvaluate() {
    const wchar_t* rtl = L"USB\\VID_0BDA&PID_2838\\00000001";
    // An RTL dongle fresh out of the box: composite parent, interface 0 without a driver.
    {
        auto f = evaluateDevices({node(rtl, L"USB\\ROOT_HUB30\\1", L"usbccgp"),
                                  node(L"USB\\VID_0BDA&PID_2838&MI_00\\7&1&0&0000", rtl, L"", 28),
                                  node(L"USB\\VID_0BDA&PID_2838&MI_01\\7&1&0&0001", rtl, L"", 28)});
        CHECK(f.size() == 1 && f[0].status == DevStatus::NoDriver);
    }
    // After Zadig: WinUSB on interface 0.
    {
        auto f = evaluateDevices({node(rtl, L"", L"usbccgp"),
                                  node(L"USB\\VID_0BDA&PID_2838&MI_00\\7&1&0&0000", rtl, L"WINUSB")});
        CHECK(f.size() == 1 && f[0].status == DevStatus::Ok);
        // The target of an automatic WinUSB install is interface 0 of this dongle.
        CHECK(f.size() == 1 && f[0].targetVid == 0x0BDA && f[0].targetPid == 0x2838 && f[0].targetMi == 0);
    }
    // Windows' DVB-T TV driver on it.
    {
        auto f = evaluateDevices({node(rtl, L"", L"usbccgp"),
                                  node(L"USB\\VID_0BDA&PID_2838&MI_00\\7&1&0&0000", rtl, L"RTL2832UBDA")});
        CHECK(f.size() == 1 && f[0].status == DevStatus::WrongDriver);
    }
    // WinUSB on the whole device (Zadig with composite parents shown).
    {
        auto f = evaluateDevices({node(rtl, L"", L"WinUSB")});
        CHECK(f.size() == 1 && f[0].status == DevStatus::Ok);
    }
    // HackRF on libusbK from some other installer.
    {
        auto f = evaluateDevices({node(L"USB\\VID_1D50&PID_6089\\123", L"", L"libusbK")});
        CHECK(f.size() == 1 && f[0].status == DevStatus::WorksLibusb);
    }
    // A Pluto whose network interface has no driver.
    {
        const wchar_t* pl = L"USB\\VID_0456&PID_B673\\104473";
        auto f = evaluateDevices({node(pl, L"", L"usbccgp"), node(L"USB\\VID_0456&PID_B673&MI_00\\8&1", pl, L"", 28),
                                  node(L"USB\\VID_0456&PID_B673&MI_02\\8&2", pl, L"usbser")});
        CHECK(f.size() == 1 && f[0].status == DevStatus::NoDriver);
    }
    // Things that are not radios are not reported.
    CHECK(evaluateDevices({node(L"USB\\VID_046D&PID_C52B\\5", L"", L"usbccgp")}).empty());
}

static ProgramFinding fakeProgram(const wchar_t* profileId, const std::wstring& dir, Arch a,
                                  std::vector<DllInfo> dlls = {}) {
    ProgramFinding f;
    f.profile = findProfile(profileId);
    f.dir = dir;
    f.arch = a;
    f.rtlDlls = std::move(dlls);
    f.exePath = joinPath(dir, L"x.exe");
    return f;
}

static const PlanItem* find(const std::vector<PlanItem>& plan, const std::wstring& key) {
    for (const PlanItem& p : plan)
        if (p.key == key) return &p;
    return nullptr;
}

static void testPlan() {
    DeviceFinding rtlNoDriver;
    rtlNoDriver.kind = matchDevice(0x0BDA, 0x2838);
    rtlNoDriver.status = DevStatus::NoDriver;
    rtlNoDriver.targetVid = 0x0BDA;
    rtlNoDriver.targetPid = 0x2838;
    rtlNoDriver.targetMi = 0;
    SystemState sys;
    sys.vcrtX64 = false;
    sys.vcrtX86 = true;

    const std::wstring cham = L"C:\\Users\\x\\AppData\\Roaming\\C-SDR\\PUT_SDR_FILES_HERE";
    const std::wstring sharp = L"D:\\sdrsharp-x86";
    std::vector<ProgramFinding> progs = {fakeProgram(L"chameleonsdr", cham, Arch::X64),
                                         fakeProgram(L"sdrsharp", sharp, Arch::X86,
                                                     {DllInfo{joinPath(sharp, L"rtlsdr.dll"), Arch::X86, 1000}}),
                                         fakeProgram(L"sdrpp", L"C:\\sdrpp", Arch::X64)};
    ProgramFinding hd = fakeProgram(L"hdsdr", L"C:\\HDSDR", Arch::X86);
    hd.siblingOk = false;  // no ExtIO for RTL there
    progs.push_back(hd);

    const auto plan = makePlan({rtlNoDriver}, progs, sys, RtlReference{});
    // The RTL dongle with no driver gets a driver action, checked: either the
    // automatic WinUSB install (libwdi build) or Zadig (fallback build).
    const bool autoDrv = driverinst::available();
    const PlanItem* drv = autoDrv ? find(plan, L"winusb:0BDA-2838-0") : find(plan, L"pkg:zadig");
    CHECK(drv && drv->checked);
    if (autoDrv) {
        CHECK(drv->type == ActionType::InstallWinUsb && drv->needsAdmin);
        CHECK(drv->vid == 0x0BDA && drv->pid == 0x2838 && drv->mi == 0);
        CHECK(drv->interfaceGuid == winUsbGuid(0x0BDA, 0x2838, 0));
        CHECK(find(plan, L"pkg:zadig") == nullptr);  // not both
    } else {
        CHECK(drv->type == ActionType::RunTool);
    }
    const PlanItem* c = find(plan, L"copy:" + lower(cham));
    CHECK(c && c->checked && c->arch == Arch::X64 && !c->replaces);
    CHECK(c && c->linkedKey == L"pkg:vcredist-x64");
    const PlanItem* vc = find(plan, L"pkg:vcredist-x64");
    CHECK(vc && vc->checked);
    CHECK(find(plan, L"pkg:vcredist-x86") == nullptr);  // the x86 runtime is there
    const PlanItem* s = find(plan, L"copy:" + lower(sharp));
    CHECK(s && !s->checked && s->replaces && s->arch == Arch::X86);  // replacing: never ticked by default
    CHECK(find(plan, L"copy:c:\\sdrpp") == nullptr);                 // ships its own
    CHECK(find(plan, L"copy:c:\\hdsdr") == nullptr);                 // no ExtIO_RTL: rtlsdr.dll not needed
    // Order: the driver action first, runtime before the copies.
    const std::wstring drvKey = autoDrv ? L"winusb:0BDA-2838-0" : L"pkg:zadig";
    size_t iz = 0, iv = 0, ic = 0;
    for (size_t i = 0; i < plan.size(); ++i) {
        if (plan[i].key == drvKey) iz = i;
        if (plan[i].key == L"pkg:vcredist-x64") iv = i;
        if (plan[i].key == L"copy:" + lower(cham)) ic = i;
    }
    CHECK(iz < iv && iv < ic);

    // No radio plugged in: copies are offered, not ticked, and no driver action.
    const auto quiet = makePlan({}, progs, sys, RtlReference{});
    const PlanItem* c2 = find(quiet, L"copy:" + lower(cham));
    CHECK(c2 && !c2->checked);
    CHECK(find(quiet, L"pkg:zadig") == nullptr);
    CHECK(find(quiet, L"winusb:0BDA-2838-0") == nullptr);

    // SDRplay radio, API missing.
    DeviceFinding rsp;
    rsp.kind = matchDevice(0x1DF7, 0x3000);
    rsp.status = DevStatus::Ok;
    const auto p3 = makePlan({rsp}, {}, sys, RtlReference{});
    const PlanItem* api = find(p3, L"pkg:sdrplay-api");
    CHECK(api && api->checked && api->type == ActionType::OpenPage);
}

static void testWinUsbGuid() {
    // Valid GUID shape, deterministic, carrying VID/PID/interface; distinct per device.
    const std::wstring g = winUsbGuid(0x0BDA, 0x2838, 0);
    CHECK(g == L"{6B3F2E31-0BDA-2838-0000-534452494E53}");
    CHECK(g.size() == 38 && g.front() == L'{' && g.back() == L'}');
    CHECK(winUsbGuid(0x1D50, 0x6089, -1) == L"{6B3F2E31-1D50-6089-FFFF-534452494E53}");
    CHECK(winUsbGuid(0x0BDA, 0x2838, 0) != winUsbGuid(0x0BDA, 0x2838, 1));
    // consentText names the device and spells out the certificate step.
    CHECK(driverinst::consentText(L"RTL2838UHIDIR").find(L"RTL2838UHIDIR") != std::wstring::npos);
    CHECK(driverinst::consentText(L"RTL2838UHIDIR").find(L"certificate") != std::wstring::npos);
}

static void testWrongDriverPlan() {
    // The DVB-T TV driver on an RTL dongle: a driver action, checked.
    DeviceFinding rtl;
    rtl.kind = matchDevice(0x0BDA, 0x2838);
    rtl.status = DevStatus::WrongDriver;
    rtl.service = L"RTL2832UBDA";
    rtl.targetVid = 0x0BDA;
    rtl.targetPid = 0x2838;
    rtl.targetMi = 0;
    const auto plan = makePlan({rtl}, {}, SystemState{true, true, false, L""}, RtlReference{});
    const PlanItem* drv =
        driverinst::available() ? find(plan, L"winusb:0BDA-2838-0") : find(plan, L"pkg:zadig");
    CHECK(drv && drv->checked);
    // A HackRF already on WinUSB needs no driver action at all.
    DeviceFinding hackrf;
    hackrf.kind = matchDevice(0x1D50, 0x6089);
    hackrf.status = DevStatus::Ok;
    hackrf.targetVid = 0x1D50;
    hackrf.targetPid = 0x6089;
    hackrf.targetMi = -1;
    const auto p2 = makePlan({hackrf}, {}, SystemState{true, true, false, L""}, RtlReference{});
    CHECK(find(p2, L"winusb:1D50-6089--1") == nullptr);
    CHECK(find(p2, L"pkg:zadig") == nullptr);
}

static void testSameFileSkipped() {
    const std::wstring d = tempDir(L"same");
    const std::wstring dll = joinPath(d, L"rtlsdr.dll");
    writeFile(dll, "pretend this is rtlsdr.dll");
    std::string h;
    sha256File(dll, h);
    ProgramFinding f = fakeProgram(L"sdrsharp", d, Arch::X64, {DllInfo{dll, Arch::X64, 26}});
    RtlReference ref;
    ref.x64 = h;
    const auto plan = makePlan({}, {f}, SystemState{true, true, false, L""}, ref);
    CHECK(find(plan, L"copy:" + lower(d)) == nullptr);  // already exactly the file it would get
    deleteTree(d);
}

static void testTransactionAndUndo() {
    const std::wstring root = tempDir(L"txn");
    const std::wstring src = joinPath(root, L"src"), dst = joinPath(root, L"dst"), backups = joinPath(root, L"backups");
    ensureDir(src);
    ensureDir(dst);
    writeFile(joinPath(src, L"rtlsdr.dll"), "NEW");
    writeFile(joinPath(src, L"libusb-1.0.dll"), "LIBUSB");
    writeFile(joinPath(dst, L"rtlsdr.dll"), "OLD");

    CHECK(!hasUndo(backups));
    {
        Transaction t(backups);
        bool changed = false;
        std::wstring err;
        CHECK(t.install(joinPath(src, L"rtlsdr.dll"), joinPath(dst, L"rtlsdr.dll"), changed, err) && changed);
        CHECK(t.install(joinPath(src, L"libusb-1.0.dll"), joinPath(dst, L"libusb-1.0.dll"), changed, err) && changed);
        CHECK(t.install(joinPath(src, L"rtlsdr.dll"), joinPath(dst, L"rtlsdr.dll"), changed, err) && !changed);
        CHECK(t.changes() == 2);
    }
    std::string s;
    CHECK(readFile(joinPath(dst, L"rtlsdr.dll"), s) && s == "NEW");
    CHECK(hasUndo(backups));
    std::wstring err;
    CHECK(undoLatest(backups, nullptr, err));
    CHECK(readFile(joinPath(dst, L"rtlsdr.dll"), s) && s == "OLD");
    CHECK(!fileExists(joinPath(dst, L"libusb-1.0.dll")));
    CHECK(!hasUndo(backups));

    // A file changed after the installation is left alone by undo.
    {
        Transaction t(backups);
        bool changed = false;
        CHECK(t.install(joinPath(src, L"rtlsdr.dll"), joinPath(dst, L"rtlsdr.dll"), changed, err));
    }
    writeFile(joinPath(dst, L"rtlsdr.dll"), "SOMEONE ELSE'S");
    CHECK(undoLatest(backups, nullptr, err));
    CHECK(readFile(joinPath(dst, L"rtlsdr.dll"), s) && s == "SOMEONE ELSE'S");
    deleteTree(root);
}

static void testVerify() {
    const std::wstring d = tempDir(L"verify");
    const std::wstring f = joinPath(d, L"pkg.bin");
    writeFile(f, "abc");
    Package p{L"t", L"t", L"t", PkgKind::Archive, L"https://x", L"pkg.bin", 3,
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", nullptr, L"", L"", nullptr,
              Payload::Never};
    std::wstring err;
    CHECK(verifyPackageFile(p, f, err));
    p.size = 4;
    CHECK(!verifyPackageFile(p, f, err));
    p.size = 3;
    p.sha256 = "0000000000000000000000000000000000000000000000000000000000000000";
    CHECK(!verifyPackageFile(p, f, err));
    p.sha256 = nullptr;
    p.signer = L"Microsoft Corporation";
    CHECK(!verifyPackageFile(p, f, err));  // not signed at all
    deleteTree(d);
}

static void testCatalog() {
    std::set<std::wstring> ids;
    for (const Package& p : packages()) {
        CHECK(ids.insert(p.id).second);
        CHECK(istartsWith(p.url, L"https://"));
        // Every downloadable file is pinned one way or the other.
        if (p.kind != PkgKind::WebPage) CHECK(p.sha256 != nullptr || p.signer != nullptr);
        if (p.sha256) CHECK(std::string(p.sha256).size() == 64);
        // Only what may be redistributed can go in an offline package, and only with its source.
        if (p.payload != Payload::Never) CHECK(p.sourceArchive != nullptr);
    }
    for (const DeviceKind& k : deviceKinds())
        if (k.fixPackage) CHECK(findPackage(k.fixPackage) != nullptr);
    for (const ProgramProfile& p : programProfiles()) {
        CHECK(!p.exeNames.empty());
        if (p.needsPackage) CHECK(findPackage(p.needsPackage) != nullptr);
        if (p.rtl == RtlPolicy::FixedDir) CHECK(p.fixedDir != nullptr && p.fixedArch != Arch::Unknown);
    }
    CHECK(profileForExe(L"sdrsharp.EXE") == findProfile(L"sdrsharp"));
    CHECK(profileForExe(L"SDRSharp.dotnet8.exe") == findProfile(L"sdrsharp"));
    CHECK(profileForExe(L"notepad.exe") == nullptr);
}

static void testShadowDll() {
    // ChameleonSDR loads an rtlsdr.dll beside its .exe before its drop folder: then no copy is proposed.
    const std::wstring d = tempDir(L"shadow");
    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, &self[0], static_cast<DWORD>(self.size())));
    const std::wstring exe = joinPath(d, L"ChameleonSDR.exe");
    CopyFileW(self.c_str(), exe.c_str(), FALSE);
    DeviceFinding rtl;
    rtl.kind = matchDevice(0x0BDA, 0x2838);
    rtl.status = DevStatus::Ok;
    const SystemState sys{true, true, false, L""};

    ProgramFinding f = describeProgram(*findProfile(L"chameleonsdr"), exe, L"test");
    CHECK(f.shadowDll.empty());
    CHECK(f.dir == expandEnv(L"%APPDATA%\\C-SDR\\PUT_SDR_FILES_HERE"));
    auto plan = makePlan({rtl}, {f}, sys, RtlReference{});
    // (proposed unless the drop folder already has one)
    CHECK(f.rtlDlls.empty() == (find(plan, L"copy:" + lower(f.dir)) != nullptr));

    writeFile(joinPath(d, L"rtlsdr.dll"), "x");
    f = describeProgram(*findProfile(L"chameleonsdr"), exe, L"test");
    CHECK(iequals(f.shadowDll, joinPath(d, L"rtlsdr.dll")));
    plan = makePlan({rtl}, {f}, sys, RtlReference{});
    CHECK(find(plan, L"copy:" + lower(f.dir)) == nullptr);
    deleteTree(d);
}

static void testDescribe() {
    const std::wstring d = tempDir(L"describe");
    std::wstring self(32768, L'\0');
    self.resize(GetModuleFileNameW(nullptr, &self[0], static_cast<DWORD>(self.size())));
    const std::wstring exe = joinPath(d, L"HDSDR.exe");
    CopyFileW(self.c_str(), exe.c_str(), FALSE);
    ProgramFinding f = describeProgram(*findProfile(L"hdsdr"), exe, L"test");
    CHECK(f.arch == Arch::X64 && !f.siblingOk && f.rtlDlls.empty());
    writeFile(joinPath(d, L"ExtIO_RTL2832.dll"), "x");
    CopyFileW(self.c_str(), joinPath(d, L"rtlsdr.dll").c_str(), FALSE);
    f = describeProgram(*findProfile(L"hdsdr"), exe, L"test");
    CHECK(f.siblingOk && f.rtlDlls.size() == 1 && f.rtlDlls[0].arch == Arch::X64);
    ProgramScanOptions o;
    o.standard = false;
    o.extraFolders = {d};
    const ProgramScanResult r = scanPrograms(o);
    bool found = false;
    for (const ProgramFinding& p : r.programs)
        if (std::wstring(p.profile->id) == L"hdsdr" && iequals(p.exePath, exe)) found = true;
    CHECK(found);
    deleteTree(d);
}

int wmain() {
    setLanguage(Lang::En);
    testStrings();
    testSha256();
    testPe();
    testSignature();
    testUsbIds();
    testEvaluate();
    testPlan();
    testWinUsbGuid();
    testWrongDriverPlan();
    testSameFileSkipped();
    testTransactionAndUndo();
    testVerify();
    testCatalog();
    testShadowDll();
    testDescribe();
    std::printf("%d passed, %d failed\n", g_passed, g_failed);
    return g_failed ? 1 : 0;
}

#include "gui.hpp"

#include "actions.hpp"
#include "planner.hpp"
#include "progscan.hpp"
#include "usbscan.hpp"
#include "version.hpp"

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>

namespace sdi {

namespace {

enum : UINT {
    WM_APP_LOG = WM_APP + 1,  // lParam: std::wstring*
    WM_APP_STATUS,            // lParam: std::wstring*
    WM_APP_PROGRESS,          // wParam: per mille
    WM_APP_SCANNED,           // lParam: ScanOutput*
    WM_APP_RAN,               // lParam: RunResult*
    WM_APP_UNDONE,            // lParam: std::wstring* (an error, or empty)
    WM_APP_CONFIRM,           // lParam: ConfirmRequest*; returns 1 for yes
};

enum : int {
    IDC_INTRO = 100,
    IDC_LBL_DEV,
    IDC_LV_DEV,
    IDC_LBL_PROG,
    IDC_LV_PROG,
    IDC_LBL_PLAN,
    IDC_LV_PLAN,
    IDC_RESCAN,
    IDC_ADDFOLDER,
    IDC_DEEP,
    IDC_INSTALL,
    IDC_UNDO,
    IDC_ABOUT,
    IDC_PROGRESS,
    IDC_STATUS,
    IDC_LOG,
};

struct ScanOutput {
    std::vector<DeviceFinding> devices;
    ProgramScanResult programs;
    SystemState sys;
    std::vector<PlanItem> plan;
};

struct ConfirmRequest {
    std::wstring title, text;
};

// One window, one worker at a time.
struct App {
    HINSTANCE inst = nullptr;
    HWND wnd = nullptr, intro = nullptr, lblDev = nullptr, lvDev = nullptr, lblProg = nullptr, lvProg = nullptr,
         lblPlan = nullptr, lvPlan = nullptr, rescan = nullptr, addFolder = nullptr, deep = nullptr, install = nullptr,
         undo = nullptr, about = nullptr, progress = nullptr, status = nullptr, log = nullptr;
    HFONT font = nullptr, bold = nullptr;
    double scale = 1.0;
    bool elevated = false;
    std::vector<DeviceFinding> devices;
    ProgramScanResult programs;
    SystemState sys;
    std::vector<PlanItem> plan;
    std::vector<std::wstring> extraFolders;
    std::thread worker;
    std::atomic<bool> cancel{false};
    bool busy = false, scanningDeep = false, filling = false, detached = false;
};

App g;

int S(int px) { return static_cast<int>(px * g.scale + 0.5); }

template <class T>
void post(UINT msg, T* payload, WPARAM w = 0) {
    if (!g.wnd || !PostMessageW(g.wnd, msg, w, reinterpret_cast<LPARAM>(payload))) delete payload;
}
void postLog(const std::wstring& s) { post(WM_APP_LOG, new std::wstring(s)); }
void postStatus(const std::wstring& s) { post(WM_APP_STATUS, new std::wstring(s)); }

void appendLog(const std::wstring& s) {
    const int len = GetWindowTextLengthW(g.log);
    SendMessageW(g.log, EM_SETSEL, len, len);
    const std::wstring line = s + L"\r\n";
    SendMessageW(g.log, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(line.c_str()));
}

void setStatus(const std::wstring& s) { SetWindowTextW(g.status, s.c_str()); }

void updateButtons() {
    const bool b = g.busy;
    EnableWindow(g.rescan, !b);
    EnableWindow(g.addFolder, !b);
    EnableWindow(g.install, !b && !g.plan.empty());
    EnableWindow(g.undo, !b && hasUndo(defaultContext().backupsDir));
    EnableWindow(g.deep, !b || g.scanningDeep);
    SetWindowTextW(g.deep, b && g.scanningDeep ? L"Stop the full scan"
                                               : L"Full scan of all drives...");
    EnableWindow(g.lvPlan, !b);
}

void startWorker(std::function<void()> fn) {
    if (g.worker.joinable()) g.worker.join();  // the previous one has finished: busy was false
    g.cancel = false;
    g.busy = true;
    updateButtons();
    g.worker = std::thread([fn] {
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        fn();
        if (SUCCEEDED(com)) CoUninitialize();
    });
}

// ------------------------------------------------------------------ lists

HWND makeList(int id, bool checks) {
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL, 0, 0,
                             0, 0, g.wnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    DWORD ex = LVS_EX_FULLROWSELECT | LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER | LVS_EX_INFOTIP;
    if (checks) ex |= LVS_EX_CHECKBOXES;
    ListView_SetExtendedListViewStyle(h, ex);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    return h;
}

void addColumn(HWND lv, int i, const wchar_t* text, int width) {
    LVCOLUMNW c{};
    c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.pszText = const_cast<LPWSTR>(text);
    c.cx = S(width);
    c.iSubItem = i;
    ListView_InsertColumn(lv, i, &c);
}

int addRow(HWND lv, const std::vector<std::wstring>& cells, LPARAM param) {
    LVITEMW it{};
    it.mask = LVIF_TEXT | LVIF_PARAM;
    it.iItem = ListView_GetItemCount(lv);
    it.pszText = const_cast<LPWSTR>(cells[0].c_str());
    it.lParam = param;
    const int row = ListView_InsertItem(lv, &it);
    for (size_t i = 1; i < cells.size(); ++i)
        ListView_SetItemText(lv, row, static_cast<int>(i), const_cast<LPWSTR>(cells[i].c_str()));
    return row;
}

std::wstring dllText(const std::vector<DllInfo>& dlls) {
    if (dlls.empty()) return L"-";
    std::wstring s;
    for (const DllInfo& d : dlls) {
        if (!s.empty()) s += L"; ";
        s += fileName(d.path) + L" " + archName(d.arch) + L", " + formatBytes(d.size);
    }
    return s;
}

void fillLists() {
    g.filling = true;
    ListView_DeleteAllItems(g.lvDev);
    ListView_DeleteAllItems(g.lvProg);
    ListView_DeleteAllItems(g.lvPlan);

    for (size_t i = 0; i < g.devices.size(); ++i) {
        const DeviceFinding& d = g.devices[i];
        addRow(g.lvDev,
               {d.kind->name, d.service.empty() ? std::wstring(L"(none)") : d.service, statusText(d.status),
                adviceFor(d)},
               static_cast<LPARAM>(i));
    }
    if (g.devices.empty())
        addRow(g.lvDev, {L"No known SDR radio is plugged in.", L"",
                         L"", L"Plug it in and press Scan again."},
               -1);

    for (size_t i = 0; i < g.programs.programs.size(); ++i) {
        const ProgramFinding& f = g.programs.programs[i];
        std::wstring dlls = dllText(f.rtlDlls);
        if (f.profile->rtl == RtlPolicy::ShipsOwn) dlls = L"its own";
        if (f.profile->rtl == RtlPolicy::None) dlls = L"not used";
        if (f.profile->requireSibling && !f.siblingOk)
            dlls += L" (no ExtIO for RTL here)";
        if (!f.shadowDll.empty())
            dlls = f.shadowDll + L" (loaded first)";
        addRow(g.lvProg,
               {f.profile->name, f.dir.empty() ? f.exePath : f.dir, archName(f.arch), dlls, f.foundBy},
               static_cast<LPARAM>(i));
    }
    for (const DllInfo& d : g.programs.looseDlls)
        addRow(g.lvProg,
               {L"rtlsdr.dll (no program recognised)", parentDir(d.path),
                archName(d.arch), formatBytes(d.size), L""},
               -1);
    if (g.programs.programs.empty() && g.programs.looseDlls.empty())
        addRow(g.lvProg,
               {L"No known SDR program found.",
                L"A portable program elsewhere? Use \"Add folder\" or the full scan.",
                L"", L"", L""},
               -1);

    for (size_t i = 0; i < g.plan.size(); ++i) {
        const PlanItem& p = g.plan[i];
        const Package* pkg = findPackage(p.packageId);
        std::wstring licence = pkg ? pkg->licence : L"";
        if (p.type == ActionType::InstallWinUsb)
            licence = L"LGPL-2.1-or-later (libwdi); WinUSB: Microsoft (in Windows)";
        const int row = addRow(g.lvPlan, {p.title, p.reason, licence}, static_cast<LPARAM>(i));
        ListView_SetCheckState(g.lvPlan, row, p.checked);
    }
    if (g.plan.empty())
        addRow(g.lvPlan, {L"Nothing to do -- everything found is in order.",
                          L"", L""},
               -1);
    g.filling = false;
}

void setChecked(const std::wstring& key, bool on) {
    for (int row = 0, n = ListView_GetItemCount(g.lvPlan); row < n; ++row) {
        LVITEMW it{};
        it.mask = LVIF_PARAM;
        it.iItem = row;
        if (!ListView_GetItem(g.lvPlan, &it) || it.lParam < 0) continue;
        PlanItem& p = g.plan[static_cast<size_t>(it.lParam)];
        if (p.key == key && p.checked != on) {
            p.checked = on;
            ListView_SetCheckState(g.lvPlan, row, on);
        }
    }
}

// ------------------------------------------------------------------ work

void startScan(bool deep) {
    ProgramScanOptions o;
    o.deep = deep;
    o.extraFolders = g.extraFolders;
    o.cancel = &g.cancel;
    o.status = [](const std::wstring& s) { postStatus(std::wstring(L"Looking in: ") + s); };
    g.scanningDeep = deep;
    setStatus(L"Scanning...");
    startWorker([o] {
        auto out = std::make_unique<ScanOutput>();
        postStatus(L"Checking USB devices...");
        out->devices = evaluateDevices(enumerateUsb());
        out->programs = scanPrograms(o);
        out->sys = probeSystem();
        ActionContext c = defaultContext();
        const RtlReference ref = rtlReference(c);
        out->plan = makePlan(out->devices, out->programs.programs, out->sys, ref);
        post(WM_APP_SCANNED, out.release());
    });
}

void startRun(std::vector<PlanItem> items) {
    g.scanningDeep = false;
    appendLog(L"");
    appendLog(L"=== Installing ===");
    startWorker([items] {
        ActionContext c = defaultContext();
        const auto toFile = c.log;
        c.log = [toFile](const std::wstring& s) {
            toFile(s);
            postLog(s);
        };
        auto last = std::make_shared<int>(-1);
        c.progress = [last](uint64_t got, uint64_t total) {
            const int pm = total ? static_cast<int>(got * 1000 / total) : 0;
            if (pm != *last) {
                *last = pm;
                if (g.wnd) PostMessageW(g.wnd, WM_APP_PROGRESS, static_cast<WPARAM>(pm), 0);
            }
        };
        c.confirm = [](const std::wstring& title, const std::wstring& text) {
            ConfirmRequest r{title, text};
            return SendMessageW(g.wnd, WM_APP_CONFIRM, 0, reinterpret_cast<LPARAM>(&r)) == 1;
        };
        c.cancel = &g.cancel;
        post(WM_APP_RAN, new RunResult(runPlan(items, c)));
    });
}

void startUndo() {
    g.scanningDeep = false;
    appendLog(L"");
    appendLog(L"=== Undoing the last installation ===");
    startWorker([] {
        ActionContext c = defaultContext();
        const auto toFile = c.log;
        auto log = [toFile](const std::wstring& s) {
            toFile(s);
            postLog(s);
        };
        std::wstring err;
        undoLatest(c.backupsDir, log, err);
        post(WM_APP_UNDONE, new std::wstring(err));
    });
}

bool pickFolder(std::wstring& out) {
    IFileOpenDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_IFileOpenDialog,
                                reinterpret_cast<void**>(&d))))
        return false;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
    d->SetTitle(L"Folder with an SDR program");
    bool ok = false;
    if (SUCCEEDED(d->Show(g.wnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                out = p;
                ok = true;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    d->Release();
    return ok;
}

bool relaunchElevated() {
    std::wstring exe(32768, L'\0');
    exe.resize(GetModuleFileNameW(nullptr, &exe[0], static_cast<DWORD>(exe.size())));
    std::wstring params = L"--elevated";
    for (const std::wstring& f : g.extraFolders) params += L" --folder " + quoteArg(f);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.lpVerb = L"runas";
    sei.lpFile = exe.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_SHOWNORMAL;
    sei.hwnd = g.wnd;
    return ShellExecuteExW(&sei) != FALSE;
}

void onInstall() {
    std::vector<PlanItem> items;
    for (const PlanItem& p : g.plan)
        if (p.checked) items.push_back(p);
    if (items.empty()) {
        MessageBoxW(g.wnd, L"Tick at least one action first.",
                    SDI_PRODUCT_W, MB_OK | MB_ICONINFORMATION);
        return;
    }
    std::wstring locked;
    bool needsAdmin = false;
    for (const PlanItem& p : items) {
        if (p.type == ActionType::CopyRtlSdr && !canWriteDir(p.targetDir)) locked += L"\n  " + p.targetDir;
        if (p.needsAdmin) needsAdmin = true;
    }
    if ((!locked.empty() || needsAdmin) && !g.elevated) {
        std::wstring reason;
        if (needsAdmin)
            reason += L"\n  installing a USB driver";
        if (!locked.empty())
            reason += L"\n  writing to:" + locked;
        const std::wstring q =
            std::wstring(L"These need administrator rights:") +
            reason +
            L"\n\nRestart this program as administrator?\n(No: carry on without -- those steps will fail.)";
        const int a = MessageBoxW(g.wnd, q.c_str(), SDI_PRODUCT_W, MB_YESNOCANCEL | MB_ICONQUESTION);
        if (a == IDCANCEL) return;
        if (a == IDYES) {
            if (relaunchElevated())
                DestroyWindow(g.wnd);
            else
                appendLog(L"The restart as administrator was declined.");
            return;
        }
    }
    std::wstring list;
    for (const PlanItem& p : items) list += L"\n  - " + p.title;
    const std::wstring q =
        std::wstring(L"These actions will now be carried out:") + list +
        L"\n\nEvery file is checked against the publisher's before use. A file that is replaced is backed up first "
           L"(\"Undo last installation\" puts it back). Manufacturers' installers show their own licences.\n\nContinue?";
    if (MessageBoxW(g.wnd, q.c_str(), SDI_PRODUCT_W, MB_YESNO | MB_ICONQUESTION) == IDYES) startRun(items);
}

void onAbout() {
    std::wstring t = std::wstring(SDI_PRODUCT_W L" " SDI_VERSION_W L"\n") +
                     L"Open source, MIT licence. Not affiliated with any manufacturer or program it lists; their "
                        L"names only say what it works with.\n\n" +
                     L"What it can install, and under which licence:\n";
    for (const Package& p : packages()) {
        t += std::wstring(L"\n- ") + p.name + L"\n   " + p.licence + L"\n   " + p.source;
        if (p.payload == Payload::Never)
            t += L"\n   (always fetched from the publisher)";
    }
    t += L"\n\nFiles come only from the publishers' own addresses and are checked (size and SHA-256, or the digital "
            L"signature) before use. Nothing about this computer is sent anywhere.\n\nData folder: " +
         dataDir();
    MessageBoxW(g.wnd, t.c_str(), SDI_PRODUCT_W, MB_OK | MB_ICONINFORMATION);
}

// ------------------------------------------------------------------ window

void layout() {
    RECT rc;
    GetClientRect(g.wnd, &rc);
    const int W = rc.right, H = rc.bottom, m = S(10), gap = S(6), lbl = S(20), btnH = S(28), prH = S(20);
    const int introH = S(36);
    int y = m;
    MoveWindow(g.intro, m, y, W - 2 * m, introH, TRUE);
    y += introH + gap;
    const int avail = std::max(S(200), H - y - m - 3 * lbl - btnH - prH - 5 * gap);
    const int devH = avail * 18 / 100, progH = avail * 22 / 100, planH = avail * 30 / 100;
    const int logH = avail - devH - progH - planH;
    auto section = [&](HWND label, HWND list, int h) {
        MoveWindow(label, m, y, W - 2 * m, lbl, TRUE);
        y += lbl;
        MoveWindow(list, m, y, W - 2 * m, h, TRUE);
        y += h + gap;
    };
    section(g.lblDev, g.lvDev, devH);
    section(g.lblProg, g.lvProg, progH);
    section(g.lblPlan, g.lvPlan, planH);
    HWND buttons[] = {g.rescan, g.addFolder, g.deep, g.install, g.undo, g.about};
    const int n = 6, bw = std::min(S(190), (W - 2 * m - (n - 1) * gap) / n);
    for (int i = 0; i < n; ++i) MoveWindow(buttons[i], m + i * (bw + gap), y, bw, btnH, TRUE);
    y += btnH + gap;
    MoveWindow(g.progress, m, y + S(3), S(180), prH - S(6), TRUE);
    MoveWindow(g.status, m + S(180) + gap, y, W - 2 * m - S(180) - gap, prH, TRUE);
    y += prH + gap;
    MoveWindow(g.log, m, y, W - 2 * m, std::max(S(50), logH), TRUE);
}

HWND child(const wchar_t* cls, const wchar_t* text, DWORD style, int id, DWORD exStyle = 0) {
    HWND h = CreateWindowExW(exStyle, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 0, 0, g.wnd,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), g.inst, nullptr);
    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.font), TRUE);
    return h;
}

void createControls() {
    g.intro = child(L"STATIC",
                    L"Finds the SDR radios plugged into this computer and the SDR programs on it, and proposes what to "
                       L"install. Nothing changes until you press \"Install ticked\"; a file that is replaced is "
                       L"backed up first.",
                    SS_LEFT, IDC_INTRO);
    g.lblDev = child(L"STATIC", L"SDR radios plugged in", SS_LEFT, IDC_LBL_DEV);
    g.lvDev = makeList(IDC_LV_DEV, false);
    addColumn(g.lvDev, 0, L"Radio", 190);
    addColumn(g.lvDev, 1, L"Driver", 110);
    addColumn(g.lvDev, 2, L"Status", 110);
    addColumn(g.lvDev, 3, L"What it means", 560);

    g.lblProg = child(L"STATIC", L"SDR programs on this computer", SS_LEFT, IDC_LBL_PROG);
    g.lvProg = makeList(IDC_LV_PROG, false);
    addColumn(g.lvProg, 0, L"Program", 190);
    addColumn(g.lvProg, 1, L"Folder for rtlsdr.dll", 380);
    addColumn(g.lvProg, 2, L"Build", 55);
    addColumn(g.lvProg, 3, L"rtlsdr.dll", 200);
    addColumn(g.lvProg, 4, L"Found by", 130);

    g.lblPlan = child(L"STATIC", L"Proposed actions -- tick what should be done",
                      SS_LEFT, IDC_LBL_PLAN);
    g.lvPlan = makeList(IDC_LV_PLAN, true);
    addColumn(g.lvPlan, 0, L"Action", 420);
    addColumn(g.lvPlan, 1, L"Why", 380);
    addColumn(g.lvPlan, 2, L"Licence", 260);

    for (HWND h : {g.lblDev, g.lblProg, g.lblPlan}) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g.bold), TRUE);

    g.rescan = child(L"BUTTON", L"Scan again", BS_PUSHBUTTON | WS_TABSTOP, IDC_RESCAN);
    g.addFolder = child(L"BUTTON", L"Add folder...", BS_PUSHBUTTON | WS_TABSTOP, IDC_ADDFOLDER);
    g.deep = child(L"BUTTON", L"", BS_PUSHBUTTON | WS_TABSTOP, IDC_DEEP);
    g.install = child(L"BUTTON", L"Install ticked", BS_DEFPUSHBUTTON | WS_TABSTOP, IDC_INSTALL);
    g.undo = child(L"BUTTON", L"Undo last installation", BS_PUSHBUTTON | WS_TABSTOP, IDC_UNDO);
    g.about = child(L"BUTTON", L"Licences and about", BS_PUSHBUTTON | WS_TABSTOP, IDC_ABOUT);
    g.progress = child(PROGRESS_CLASSW, L"", PBS_SMOOTH, IDC_PROGRESS);
    SendMessageW(g.progress, PBM_SETRANGE32, 0, 1000);
    g.status = child(L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX, IDC_STATUS);
    g.log = child(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP, IDC_LOG,
                  WS_EX_CLIENTEDGE);
    SendMessageW(g.log, EM_SETLIMITTEXT, 4 << 20, 0);
}

void onScanned(std::unique_ptr<ScanOutput> out) {
    g.devices = std::move(out->devices);
    g.programs = std::move(out->programs);
    g.sys = out->sys;
    g.plan = std::move(out->plan);
    g.busy = false;
    fillLists();
    updateButtons();
    int ticked = 0;
    for (const PlanItem& p : g.plan) ticked += p.checked;
    wchar_t b[256];
    swprintf(b, 256, L"Found %zu radio(s) and %zu program(s); %zu proposed action(s), %d ticked.%ls",
             g.devices.size(), g.programs.programs.size(), g.plan.size(), ticked,
             g.programs.cancelled ? L" (scan stopped early)" : L"");
    setStatus(b);
    appendLog(b);
    SendMessageW(g.progress, PBM_SETPOS, 0, 0);
}

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            g.wnd = h;
            createControls();
            return 0;
        case WM_SIZE:
            layout();
            return 0;
        case WM_GETMINMAXINFO: {
            auto* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = S(820);
            mm->ptMinTrackSize.y = S(600);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wp)) {
                case IDC_RESCAN: startScan(false); break;
                case IDC_ADDFOLDER: {
                    std::wstring f;
                    if (pickFolder(f)) {
                        g.extraFolders.push_back(f);
                        appendLog(std::wstring(L"Folder added: ") + f);
                        startScan(false);
                    }
                    break;
                }
                case IDC_DEEP:
                    if (g.busy && g.scanningDeep) {
                        g.cancel = true;
                        setStatus(L"Stopping...");
                    } else if (MessageBoxW(h,
                                           L"The full scan looks at the file names on every local drive to find SDR "
                                              L"programs. It can take a few minutes. Nothing is changed, and nothing "
                                              L"leaves this computer.\n\nStart it?",
                                           SDI_PRODUCT_W, MB_YESNO | MB_ICONQUESTION) == IDYES) {
                        startScan(true);
                    }
                    break;
                case IDC_INSTALL: onInstall(); break;
                case IDC_UNDO:
                    if (MessageBoxW(h,
                                    L"Put back the files the last installation replaced, and remove the ones it "
                                       L"added? (Drivers installed by Zadig or by a manufacturer's installer are "
                                       L"removed in Windows' Device Manager or Apps.)",
                                    SDI_PRODUCT_W, MB_YESNO | MB_ICONQUESTION) == IDYES)
                        startUndo();
                    break;
                case IDC_ABOUT: onAbout(); break;
            }
            return 0;
        case WM_NOTIFY: {
            auto* nh = reinterpret_cast<NMHDR*>(lp);
            if (nh->code == LVN_ITEMCHANGED) {
                auto* nv = reinterpret_cast<NMLISTVIEW*>(lp);
                if (nv->lParam < 0) return 0;
                const size_t i = static_cast<size_t>(nv->lParam);
                if (nh->hwndFrom == g.lvPlan && i < g.plan.size()) {
                    if (!g.filling && (nv->uChanged & LVIF_STATE) &&
                        ((nv->uNewState ^ nv->uOldState) & LVIS_STATEIMAGEMASK)) {
                        const bool on = ((nv->uNewState & LVIS_STATEIMAGEMASK) >> 12) == 2;
                        g.plan[i].checked = on;
                        if (on && !g.plan[i].linkedKey.empty()) setChecked(g.plan[i].linkedKey, true);
                    }
                    if ((nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED))
                        setStatus(g.plan[i].title + L" -- " + g.plan[i].reason);
                } else if (nh->hwndFrom == g.lvDev && i < g.devices.size() && (nv->uNewState & LVIS_SELECTED) &&
                           !(nv->uOldState & LVIS_SELECTED)) {
                    setStatus(std::wstring(g.devices[i].kind->name) + L": " + adviceFor(g.devices[i]) + L"  [" +
                              g.devices[i].instanceId + L"]");
                } else if (nh->hwndFrom == g.lvProg && i < g.programs.programs.size() &&
                           (nv->uNewState & LVIS_SELECTED) && !(nv->uOldState & LVIS_SELECTED)) {
                    const ProgramFinding& f = g.programs.programs[i];
                    setStatus(std::wstring(f.profile->name) + L": " + f.profile->note +
                              (f.exePath.empty() ? L"" : L"  [" + f.exePath + L"]"));
                }
            } else if (nh->code == NM_DBLCLK && nh->hwndFrom == g.lvProg) {
                auto* ia = reinterpret_cast<NMITEMACTIVATE*>(lp);
                LVITEMW it{};
                it.mask = LVIF_PARAM;
                it.iItem = ia->iItem;
                if (ia->iItem >= 0 && ListView_GetItem(g.lvProg, &it) && it.lParam >= 0 &&
                    static_cast<size_t>(it.lParam) < g.programs.programs.size())
                    openFolder(g.programs.programs[static_cast<size_t>(it.lParam)].dir);
            }
            return 0;
        }
        case WM_APP_LOG: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            appendLog(*s);
            return 0;
        }
        case WM_APP_STATUS: {
            std::unique_ptr<std::wstring> s(reinterpret_cast<std::wstring*>(lp));
            setStatus(*s);
            return 0;
        }
        case WM_APP_PROGRESS:
            SendMessageW(g.progress, PBM_SETPOS, wp, 0);
            return 0;
        case WM_APP_SCANNED:
            onScanned(std::unique_ptr<ScanOutput>(reinterpret_cast<ScanOutput*>(lp)));
            return 0;
        case WM_APP_RAN: {
            std::unique_ptr<RunResult> r(reinterpret_cast<RunResult*>(lp));
            wchar_t b[200];
            swprintf(b, 200, L"Finished: %d done, %d failed, %d skipped. Checking again...",
                     r->done, r->failed, r->skipped);
            appendLog(b);
            g.busy = false;
            SendMessageW(g.progress, PBM_SETPOS, 0, 0);
            startScan(false);
            return 0;
        }
        case WM_APP_UNDONE: {
            std::unique_ptr<std::wstring> e(reinterpret_cast<std::wstring*>(lp));
            if (!e->empty()) appendLog(L"! " + *e);
            g.busy = false;
            startScan(false);
            return 0;
        }
        case WM_APP_CONFIRM: {
            auto* r = reinterpret_cast<ConfirmRequest*>(lp);
            return MessageBoxW(h, r->text.c_str(), r->title.c_str(), MB_OKCANCEL | MB_ICONINFORMATION) == IDOK ? 1 : 0;
        }
        case WM_CLOSE:
            if (g.busy &&
                MessageBoxW(h,
                            L"Work is still in progress (an installer may be open). Close anyway?",
                            SDI_PRODUCT_W, MB_YESNO | MB_ICONWARNING) != IDYES)
                return 0;
            g.cancel = true;
            DestroyWindow(h);
            return 0;
        case WM_DESTROY:
            g.wnd = nullptr;
            if (g.worker.joinable()) {
                if (g.busy) {
                    g.worker.detach();  // an installer it waits for may stay open
                    g.detached = true;
                } else {
                    g.worker.join();
                }
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int runGui(HINSTANCE inst, const std::vector<std::wstring>& extraFolders, bool elevatedRestart) {
    g.inst = inst;
    g.extraFolders = extraFolders;
    g.elevated = isElevated();
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof icc, ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    HDC dc = GetDC(nullptr);
    g.scale = GetDeviceCaps(dc, LOGPIXELSY) / 96.0;
    ReleaseDC(nullptr, dc);
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof ncm;
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    g.bold = CreateFontIndirectW(&bold);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof wc;
    wc.lpfnWndProc = wndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SDRLoaderWindow";
    RegisterClassExW(&wc);

    std::wstring title = SDI_PRODUCT_W L" " SDI_VERSION_W;
    if (g.elevated) title += L" (administrator)";
    HWND w = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                             S(1180), S(860), nullptr, nullptr, inst, nullptr);
    if (!w) return 1;
    ShowWindow(w, SW_SHOWNORMAL);
    UpdateWindow(w);
    updateButtons();
    if (elevatedRestart)
        appendLog(L"Running as administrator: the scan is repeated, then press Install again.");
    startScan(false);

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (IsDialogMessageW(w, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (g.detached) ExitProcess(0);  // do not tear down what a still-running worker uses
    DeleteObject(g.font);
    DeleteObject(g.bold);
    CoUninitialize();
    return 0;
}

}  // namespace sdi

#include "progscan.hpp"

#include "i18n.hpp"

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <map>
#include <set>

namespace sdi {

namespace {

const wchar_t* const kRtlNames[] = {L"rtlsdr.dll", L"librtlsdr.dll"};

bool isRtlName(const std::wstring& n) {
    for (const wchar_t* r : kRtlNames)
        if (iequals(n, r)) return true;
    return false;
}

// Folders never descended into: system areas, caches, version-control trees.
bool skipDirName(const std::wstring& n) {
    static const wchar_t* const skip[] = {
        L"$Recycle.Bin", L"System Volume Information", L"Recovery", L"$WinREAgent", L"Config.Msi", L"MSOCache",
        L"PerfLogs", L"WinSxS", L"node_modules", L".git", L".svn", L"__pycache__", L"$SysReset", L"$Windows.~BT",
        L"$Windows.~WS", L"Windows.old", L"$GetCurrent"};
    for (const wchar_t* s : skip)
        if (iequals(n, s)) return true;
    return false;
}

std::wstring regValue(HKEY key, const wchar_t* name) {
    wchar_t buf[2048] = {};
    DWORD bytes = sizeof buf - sizeof(wchar_t);
    if (RegGetValueW(key, nullptr, name, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf, &bytes) != ERROR_SUCCESS)
        return {};
    return buf;
}

std::wstring unquote(std::wstring s) {
    s = trim(s);
    if (s.size() >= 2 && s.front() == L'"') {
        const size_t e = s.find(L'"', 1);
        s = s.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
    }
    return s;
}

// "C:\x\prog.exe,0" or "\"C:\x\prog.exe\",0" -> C:\x\prog.exe
std::wstring iconPath(const std::wstring& icon) {
    std::wstring s = trim(icon);
    if (!s.empty() && s.front() == L'"') return unquote(s);
    const size_t comma = s.rfind(L',');
    if (comma != std::wstring::npos && comma > 2) s = s.substr(0, comma);
    return trim(s);
}

std::wstring resolveShortcut(const std::wstring& lnk) {
    std::wstring out;
    IShellLinkW* sl = nullptr;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                reinterpret_cast<void**>(&sl))))
        return out;
    IPersistFile* pf = nullptr;
    if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&pf)))) {
        if (SUCCEEDED(pf->Load(lnk.c_str(), STGM_READ))) {
            wchar_t path[MAX_PATH] = {};
            if (SUCCEEDED(sl->GetPath(path, MAX_PATH, nullptr, SLGP_RAWPATH)) && path[0]) out = expandEnv(path);
        }
        pf->Release();
    }
    sl->Release();
    return out;
}

std::vector<std::wstring> fixedDrives() {
    std::vector<std::wstring> out;
    wchar_t buf[512] = {};
    const DWORD n = GetLogicalDriveStringsW(511, buf);
    for (const wchar_t* p = buf; n && p < buf + n && *p; p += wcslen(p) + 1)
        if (GetDriveTypeW(p) == DRIVE_FIXED) out.push_back(p);
    return out;
}

class Scanner {
public:
    Scanner(const ProgramScanOptions& o, ProgramScanResult& r) : o_(o), r_(r) {
        for (const std::wstring& s : {expandEnv(L"%SystemRoot%"), dataDir(), joinPath(moduleDir(), L"payload")})
            if (!s.empty()) skipRoots_.push_back(lower(s));
    }

    bool cancelled() const { return o_.cancel && o_.cancel->load(); }

    // Every file in `dir` and up to `depth` levels below it.
    void walk(const std::wstring& dir, int depth, const wchar_t* foundBy) {
        if (depth < 0 || cancelled()) return;
        const std::wstring key = lower(dir);
        const auto it = visited_.find(key);
        if (it != visited_.end() && it->second >= depth) return;
        visited_[key] = depth;
        for (const std::wstring& root : skipRoots_)
            if (key == root || (key.size() > root.size() && key.compare(0, root.size(), root) == 0 &&
                                key[root.size()] == L'\\'))
                return;
        if (++r_.foldersVisited % 250 == 0 && o_.status) o_.status(dir);

        std::vector<std::wstring> subdirs;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileExW(joinPath(dir, L"*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr,
                                    FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE) return;
        do {
            const std::wstring name = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (name == L"." || name == L"..") continue;
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) continue;  // junctions: no loops
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) && (fd.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM))
                    continue;
                if (skipDirName(name)) continue;
                if (depth > 0) subdirs.push_back(name);
            } else if (profileForExe(name)) {
                addExe(joinPath(dir, name), foundBy);
            } else if (isRtlName(name)) {
                const std::wstring p = joinPath(dir, name);
                dlls_.emplace(lower(p), p);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        for (const std::wstring& s : subdirs) walk(joinPath(dir, s), depth - 1, foundBy);
    }

    // Shortcuts (.lnk) in `dir` and below, followed to the program they start.
    void shortcuts(const std::wstring& dir, int depth) {
        if (depth < 0 || dir.empty() || cancelled()) return;
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(joinPath(dir, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) return;
        std::vector<std::wstring> subdirs;
        do {
            const std::wstring name = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (name != L"." && name != L".." && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    subdirs.push_back(name);
            } else if (name.size() > 4 && iequals(name.substr(name.size() - 4), L".lnk")) {
                const std::wstring target = resolveShortcut(joinPath(dir, name));
                if (!target.empty() && profileForExe(fileName(target)) && fileExists(target))
                    addExe(target, tr(L"shortcut", L"skrót"));
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        for (const std::wstring& s : subdirs) shortcuts(joinPath(dir, s), depth - 1);
    }

    void uninstallEntries(HKEY root, REGSAM view) {
        HKEY h;
        if (RegOpenKeyExW(root, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", 0, KEY_READ | view, &h) !=
            ERROR_SUCCESS)
            return;
        for (DWORD i = 0; !cancelled(); ++i) {
            wchar_t name[256];
            DWORD n = 256;
            const LONG r = RegEnumKeyExW(h, i, name, &n, nullptr, nullptr, nullptr, nullptr);
            if (r == ERROR_NO_MORE_ITEMS) break;
            if (r != ERROR_SUCCESS) continue;
            HKEY s;
            if (RegOpenKeyExW(h, name, 0, KEY_READ | view, &s) != ERROR_SUCCESS) continue;
            const std::wstring loc = unquote(regValue(s, L"InstallLocation"));
            const std::wstring icon = iconPath(regValue(s, L"DisplayIcon"));
            RegCloseKey(s);
            if (!icon.empty() && profileForExe(fileName(icon)) && fileExists(icon))
                addExe(icon, tr(L"installed programs", L"zainstalowane programy"));
            if (!loc.empty() && dirExists(loc)) walk(loc, 1, tr(L"installed programs", L"zainstalowane programy"));
        }
        RegCloseKey(h);
    }

    void addExe(const std::wstring& exe, const wchar_t* foundBy) { exes_.emplace(lower(exe), Found{exe, foundBy}); }

    void finish() {
        std::set<std::wstring> seen;
        for (const auto& e : exes_) {
            const ProgramProfile* p = profileForExe(fileName(e.second.path));
            if (!p) continue;
            ProgramFinding f = describeProgram(*p, e.second.path, e.second.by);
            const std::wstring key =
                p->rtl == RtlPolicy::FixedDir ? std::wstring(p->id) : std::wstring(p->id) + L"|" + lower(f.dir);
            if (seen.insert(key).second) r_.programs.push_back(f);
        }
        for (const ProgramProfile& p : programProfiles()) {
            if (!p.markerDir || seen.count(p.id)) continue;
            if (dirExists(expandEnv(p.markerDir))) {
                r_.programs.push_back(describeProgram(p, L"", tr(L"its data folder", L"jego folder danych")));
                seen.insert(p.id);
            }
        }
        for (const auto& d : dlls_) {
            const std::wstring dir = lower(parentDir(d.second));
            bool owned = false;
            for (const ProgramFinding& f : r_.programs)
                if (!f.dir.empty() && lower(f.dir) == dir) owned = true;
            if (owned) continue;
            DllInfo i;
            i.path = d.second;
            i.arch = peArch(d.second);
            fileSize(d.second, i.size);
            r_.looseDlls.push_back(i);
        }
        r_.cancelled = cancelled();
    }

private:
    struct Found {
        std::wstring path;
        const wchar_t* by;
    };
    const ProgramScanOptions& o_;
    ProgramScanResult& r_;
    std::vector<std::wstring> skipRoots_;
    std::map<std::wstring, int> visited_;
    std::map<std::wstring, Found> exes_;
    std::map<std::wstring, std::wstring> dlls_;
};

}  // namespace

std::vector<DllInfo> rtlDllsIn(const std::wstring& dir) {
    std::vector<DllInfo> out;
    for (const wchar_t* n : kRtlNames) {
        const std::wstring p = joinPath(dir, n);
        if (!fileExists(p)) continue;
        DllInfo i;
        i.path = p;
        i.arch = peArch(p);
        fileSize(p, i.size);
        out.push_back(i);
    }
    return out;
}

ProgramFinding describeProgram(const ProgramProfile& p, const std::wstring& exePath, const std::wstring& foundBy) {
    ProgramFinding f;
    f.profile = &p;
    f.exePath = exePath;
    f.foundBy = foundBy;
    if (p.rtl == RtlPolicy::FixedDir) {
        f.dir = expandEnv(p.fixedDir);
        f.arch = p.fixedArch;
        if (!exePath.empty())
            for (const wchar_t* rel : p.loadsFirst) {
                const std::wstring s = joinPath(parentDir(exePath), rel);
                if (fileExists(s)) {
                    f.shadowDll = s;
                    break;
                }
            }
    } else if (!exePath.empty()) {
        f.dir = parentDir(exePath);
        f.arch = peArch(exePath);
    }
    if (!f.dir.empty()) f.rtlDlls = rtlDllsIn(f.dir);
    if (p.requireSibling) {
        f.siblingOk = false;
        WIN32_FIND_DATAW fd;
        HANDLE h = f.dir.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW(joinPath(f.dir, p.requireSibling).c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wildcardMatch(p.requireSibling, fd.cFileName))
                    f.siblingOk = true;
            } while (!f.siblingOk && FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    return f;
}

ProgramScanResult scanPrograms(const ProgramScanOptions& o) {
    ProgramScanResult r;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    Scanner s(o, r);
    const wchar_t* stdScan = tr(L"standard scan", L"zwykłe skanowanie");

    if (o.standard) {
        if (o.status) o.status(tr(L"installed programs", L"zainstalowane programy"));
        s.uninstallEntries(HKEY_LOCAL_MACHINE, KEY_WOW64_64KEY);
        s.uninstallEntries(HKEY_LOCAL_MACHINE, KEY_WOW64_32KEY);
        s.uninstallEntries(HKEY_CURRENT_USER, 0);
        for (const KNOWNFOLDERID* id : {&FOLDERID_ProgramFiles, &FOLDERID_ProgramFilesX86, &FOLDERID_UserProgramFiles}) {
            const std::wstring d = knownFolder(*id);
            if (!d.empty()) s.walk(d, 3, L"Program Files");
        }
        for (const KNOWNFOLDERID* id : {&FOLDERID_Programs, &FOLDERID_CommonPrograms})
            s.shortcuts(knownFolder(*id), 4);
        for (const KNOWNFOLDERID* id : {&FOLDERID_Desktop, &FOLDERID_PublicDesktop}) s.shortcuts(knownFolder(*id), 1);
        for (const KNOWNFOLDERID* id : {&FOLDERID_Desktop, &FOLDERID_Downloads, &FOLDERID_Documents}) {
            const std::wstring d = knownFolder(*id);
            if (!d.empty()) s.walk(d, 4, stdScan);
        }
        // Folders at the root of each drive (C:\SDR, D:\Radio ...), apart from Windows' own.
        static const wchar_t* const systemTop[] = {L"Windows", L"Program Files", L"Program Files (x86)", L"ProgramData",
                                                   L"Users"};
        for (const std::wstring& drive : fixedDrives()) {
            WIN32_FIND_DATAW fd;
            HANDLE h = FindFirstFileW(joinPath(drive, L"*").c_str(), &fd);
            if (h == INVALID_HANDLE_VALUE) continue;
            std::vector<std::wstring> tops;
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
                    (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                    continue;
                const std::wstring n = fd.cFileName;
                if (n == L"." || n == L".." || skipDirName(n)) continue;
                bool sys = false;
                for (const wchar_t* t : systemTop)
                    if (iequals(n, t)) sys = true;
                if (!sys) tops.push_back(n);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
            for (const std::wstring& t : tops) s.walk(joinPath(drive, t), 3, stdScan);
        }
    }
    for (const std::wstring& f : o.extraFolders) {
        if (o.status) o.status(f);
        s.walk(f, 8, tr(L"chosen folder", L"wybrany folder"));
    }
    if (o.deep) {
        for (const std::wstring& drive : fixedDrives()) {
            if (o.status) o.status(drive);
            s.walk(drive, 24, tr(L"full scan", L"pełne skanowanie"));
        }
    }
    s.finish();
    if (SUCCEEDED(com)) CoUninitialize();
    return r;
}

}  // namespace sdi

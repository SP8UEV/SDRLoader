#include "actions.hpp"

#include "driverinstall.hpp"
#include "i18n.hpp"
#include "net.hpp"

#include <windows.h>
#include <objbase.h>

#include <algorithm>
#include <deque>
#include <set>
#include <sstream>

namespace sdi {

namespace {

bool cancelled(const ActionContext& c) { return c.cancel && c.cancel->load(); }

void say(const ActionContext& c, const std::wstring& s) {
    if (c.log) c.log(s);
}

std::wstring sizeText(uint64_t n) { return formatBytes(n); }

// The folder that holds the runtime of a given architecture.
std::wstring runtimeDirFor(Arch a) {
    const std::wstring wow = systemWow64Dir();
    if (a == Arch::X86 && !wow.empty()) return wow;
    return systemDir();
}

std::vector<std::wstring> splitTabs(const std::string& line) {
    std::vector<std::wstring> out;
    size_t s = 0;
    for (;;) {
        const size_t t = line.find('\t', s);
        out.push_back(widen(line.substr(s, t == std::string::npos ? std::string::npos : t - s)));
        if (t == std::string::npos) break;
        s = t + 1;
    }
    return out;
}

std::wstring newestRun(const std::wstring& backupsDir) {
    std::vector<std::wstring> runs;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(joinPath(backupsDir, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return {};
    do {
        const std::wstring n = fd.cFileName;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && n != L"." && n != L"..") runs.push_back(n);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(runs.rbegin(), runs.rend());  // names are timestamps: newest first
    for (const std::wstring& r : runs) {
        const std::wstring d = joinPath(backupsDir, r);
        if (fileExists(joinPath(d, L"manifest.tsv")) && !fileExists(joinPath(d, L"undone.txt"))) return d;
    }
    return {};
}

std::wstring exitCodeText(const Package& p, DWORD code) {
    wchar_t b[32];
    swprintf(b, 32, L"%lu", static_cast<unsigned long>(code));
    if (p.kind == PkgKind::VendorInstaller) {
        switch (code) {
            case 0: return tr(L"finished", L"zakończono");
            case 3010: return tr(L"finished -- Windows needs a restart", L"zakończono -- trzeba ponownie uruchomić Windows");
            case 1638: return tr(L"a newer version is already installed", L"nowsza wersja jest już zainstalowana");
            case 1602: return tr(L"cancelled in the installer", L"anulowano w instalatorze");
            default: break;
        }
    } else if (code == 0) {
        return tr(L"closed", L"zamknięto");
    }
    return std::wstring(tr(L"ended with code ", L"zakończono z kodem ")) + b;
}

}  // namespace

ActionContext defaultContext() {
    ActionContext c;
    const std::wstring data = dataDir();
    c.payloadDir = joinPath(moduleDir(), L"payload");
    c.cacheDir = joinPath(data, L"cache");
    c.backupsDir = joinPath(data, L"backups");
    const std::wstring logFile = joinPath(data, L"log.txt");
    c.log = [logFile](const std::wstring& s) {
        SYSTEMTIME t;
        GetLocalTime(&t);
        wchar_t stamp[32];
        swprintf(stamp, 32, L"%04u-%02u-%02u %02u:%02u:%02u  ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
        appendFile(logFile, narrow(stamp + s + L"\r\n"));
    };
    return c;
}

bool verifyPackageFile(const Package& p, const std::wstring& path, std::wstring& err) {
    uint64_t size = 0;
    if (!fileSize(path, size)) {
        err = tr(L"the file cannot be read", L"nie da się odczytać pliku");
        return false;
    }
    if (p.size && size != p.size) {
        err = std::wstring(tr(L"its size is ", L"ma rozmiar ")) + std::to_wstring(size) +
              tr(L" bytes, the publisher's file has ", L" bajtów, a plik wydawcy ma ") + std::to_wstring(p.size);
        return false;
    }
    if (p.sha256) {
        std::string h;
        if (!sha256File(path, h)) {
            err = tr(L"the file cannot be read", L"nie da się odczytać pliku");
            return false;
        }
        if (h != p.sha256) {
            err = tr(L"its SHA-256 is not the one the publisher published", L"jego SHA-256 nie jest tym, który opublikował wydawca");
            return false;
        }
    }
    if (p.signer) {
        std::wstring who, e;
        if (!authenticodeSigner(path, who, e)) {
            err = e;
            return false;
        }
        if (lower(who).find(lower(p.signer)) == std::wstring::npos) {
            err = std::wstring(tr(L"it is signed by \"", L"podpisał go \"")) + who + tr(L"\", not by ", L"\", a nie ") + p.signer;
            return false;
        }
    }
    return true;
}

bool obtainPackage(const Package& p, ActionContext& c, std::wstring& path, std::wstring& err) {
    if (p.kind == PkgKind::WebPage || !p.file || !*p.file) {
        err = tr(L"this is a web page, not a file", L"to strona internetowa, nie plik");
        return false;
    }
    std::vector<std::wstring> local;
    if (p.payload != Payload::Never && !c.payloadDir.empty()) local.push_back(joinPath(c.payloadDir, p.file));
    local.push_back(joinPath(c.cacheDir, p.file));
    for (const std::wstring& l : local) {
        if (!fileExists(l)) continue;
        std::wstring why;
        if (verifyPackageFile(p, l, why)) {
            say(c, std::wstring(tr(L"  using the copy already here: ", L"  używam kopii, która już tu jest: ")) + l);
            path = l;
            return true;
        }
        say(c, std::wstring(tr(L"  ignoring ", L"  pomijam ")) + l + L": " + why);
    }
    if (!ensureDir(c.cacheDir)) {
        err = tr(L"cannot create the download folder", L"nie da się utworzyć folderu pobierania");
        return false;
    }
    const std::wstring dest = joinPath(c.cacheDir, p.file);
    say(c, std::wstring(tr(L"  downloading from ", L"  pobieram z ")) + p.url +
               (p.size ? L" (" + sizeText(p.size) + L")" : std::wstring()));
    const uint64_t cap = p.size ? p.size : (300ull << 20);
    if (!net::download(p.url, dest, cap, c.progress, c.cancel, err)) return false;
    std::wstring why;
    if (!verifyPackageFile(p, dest, why)) {
        DeleteFileW(dest.c_str());
        err = std::wstring(tr(L"the downloaded file was rejected: ", L"pobrany plik odrzucono: ")) + why;
        return false;
    }
    say(c, tr(L"  checked: it is exactly the publisher's file", L"  sprawdzono: to dokładnie plik wydawcy"));
    path = dest;
    return true;
}

bool unpackArchive(const std::wstring& archive, ActionContext& c, std::wstring& dir, std::wstring& err) {
    std::string hash;
    if (!sha256File(archive, hash)) {
        err = tr(L"the archive cannot be read", L"nie da się odczytać archiwum");
        return false;
    }
    std::wstring name = fileName(archive);
    if (name.size() > 4 && iequals(name.substr(name.size() - 4), L".zip")) name.resize(name.size() - 4);
    dir = joinPath(c.cacheDir, name);
    const std::wstring marker = joinPath(dir, L".sdi-unpacked");
    std::string seen;
    if (readFile(marker, seen, 1024) && seen == hash) return true;  // already unpacked, from this very archive

    if (dirExists(dir)) deleteTree(dir);
    if (!ensureDir(dir)) {
        err = tr(L"cannot create the folder to unpack into", L"nie da się utworzyć folderu do rozpakowania");
        return false;
    }
    DWORD code = 1;
    std::wstring e;
    const std::wstring tar = joinPath(systemDir(), L"tar.exe");  // in Windows since 10 (1803)
    bool ok = fileExists(tar) &&
              runHidden(tar, quoteArg(tar) + L" -xf " + quoteArg(archive) + L" -C " + quoteArg(dir), code, e) && code == 0;
    if (!ok) {
        auto ps1 = [](std::wstring s) {
            std::wstring o;
            for (wchar_t ch : s) {
                o.push_back(ch);
                if (ch == L'\'') o.push_back(L'\'');
            }
            return L"'" + o + L"'";
        };
        const std::wstring ps = joinPath(systemDir(), L"WindowsPowerShell\\v1.0\\powershell.exe");
        const std::wstring cmd = quoteArg(ps) +
                                 L" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"Expand-Archive "
                                 L"-LiteralPath " +
                                 ps1(archive) + L" -DestinationPath " + ps1(dir) + L" -Force\"";
        ok = fileExists(ps) && runHidden(ps, cmd, code, e) && code == 0;
    }
    if (!ok) {
        err = tr(L"the archive could not be unpacked", L"nie udało się rozpakować archiwum");
        return false;
    }
    writeFile(marker, hash);
    return true;
}

std::wstring findRtlDll(const std::wstring& unpackedDir, Arch a) {
    std::vector<std::wstring> found;
    std::deque<std::pair<std::wstring, int>> todo{{unpackedDir, 0}};
    while (!todo.empty()) {
        const auto [dir, depth] = todo.front();
        todo.pop_front();
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(joinPath(dir, L"*").c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (n != L"." && n != L".." && depth < 4) todo.push_back({joinPath(dir, n), depth + 1});
            } else if (iequals(n, L"rtlsdr.dll")) {
                found.push_back(joinPath(dir, n));
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(found.begin(), found.end());
    for (const std::wstring& f : found)
        if (peArch(f) == a) return f;
    return {};
}

RtlReference rtlReference(ActionContext& c) {
    RtlReference r;
    const Package* p = findPackage(L"rtlsdr-blog");
    if (!p) return r;
    for (const std::wstring& l : {joinPath(c.payloadDir, p->file), joinPath(c.cacheDir, p->file)}) {
        std::wstring why, dir;
        if (!fileExists(l) || !verifyPackageFile(*p, l, why) || !unpackArchive(l, c, dir, why)) continue;
        const std::wstring x64 = findRtlDll(dir, Arch::X64), x86 = findRtlDll(dir, Arch::X86);
        if (!x64.empty()) sha256File(x64, r.x64);
        if (!x86.empty()) sha256File(x86, r.x86);
        break;
    }
    return r;
}

// ------------------------------------------------------------------ backups

bool Transaction::open(std::wstring& err) {
    if (!dir_.empty()) return true;
    std::wstring d = joinPath(root_, timestamp());
    for (int i = 2; dirExists(d); ++i) d = joinPath(root_, timestamp() + L"-" + std::to_wstring(i));
    if (!ensureDir(d)) {
        err = tr(L"cannot create the backup folder", L"nie da się utworzyć folderu kopii zapasowych");
        return false;
    }
    dir_ = d;
    manifest_ = joinPath(d, L"manifest.tsv");
    return true;
}

bool Transaction::install(const std::wstring& from, const std::wstring& to, bool& changed, std::wstring& err) {
    changed = false;
    std::string newHash;
    if (!sha256File(from, newHash)) {
        err = tr(L"the new file cannot be read", L"nie da się odczytać nowego pliku");
        return false;
    }
    if (!ensureDir(parentDir(to))) {
        err = std::wstring(tr(L"cannot create the folder ", L"nie da się utworzyć folderu ")) + parentDir(to);
        return false;
    }
    if (!open(err)) return false;
    std::wstring line;
    if (fileExists(to)) {
        std::string oldHash;
        if (sha256File(to, oldHash) && oldHash == newHash) return true;  // already exactly this file
        const std::wstring backup = joinPath(dir_, std::to_wstring(changes_ + 1) + L"-" + fileName(to));
        if (!CopyFileW(to.c_str(), backup.c_str(), FALSE)) {
            err = std::wstring(tr(L"the existing file could not be backed up, so it was left alone: ",
                                  L"nie udało się zrobić kopii istniejącego pliku, więc go nie ruszono: ")) +
                  lastErrorText(GetLastError());
            return false;
        }
        line = L"R\t" + to + L"\t" + backup + L"\t";
    } else {
        line = L"C\t" + to + L"\t\t";
    }
    // Written BEFORE the file: if the copy then fails, undo finds the file unchanged and leaves it be.
    if (!appendFile(manifest_, narrow(line) + newHash + "\r\n")) {
        err = tr(L"the undo information could not be written", L"nie udało się zapisać informacji do cofania");
        return false;
    }
    if (!copyFileAtomic(from, to, err)) return false;
    ++changes_;
    changed = true;
    return true;
}

bool hasUndo(const std::wstring& backupsDir) { return !newestRun(backupsDir).empty(); }

bool undoLatest(const std::wstring& backupsDir, const std::function<void(const std::wstring&)>& log,
                std::wstring& err) {
    const std::wstring run = newestRun(backupsDir);
    if (run.empty()) {
        err = tr(L"there is nothing to undo", L"nie ma czego cofać");
        return false;
    }
    std::string text;
    if (!readFile(joinPath(run, L"manifest.tsv"), text)) {
        err = tr(L"the undo information cannot be read", L"nie da się odczytać informacji do cofania");
        return false;
    }
    std::vector<std::vector<std::wstring>> entries;
    std::istringstream in(text);
    for (std::string line; std::getline(in, line);) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        std::vector<std::wstring> f = splitTabs(line);
        if (f.size() == 4) entries.push_back(f);
    }
    int restored = 0, removed = 0, kept = 0;
    for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
        const std::wstring &kind = (*it)[0], &target = (*it)[1], &backup = (*it)[2];
        const std::string installed = narrow((*it)[3]);
        std::string now;
        const bool present = fileExists(target);
        const bool ours = present && sha256File(target, now) && now == installed;
        if (kind == L"R") {
            if (present && !ours) {
                ++kept;
                if (log) log(std::wstring(tr(L"  left alone (changed since): ", L"  bez zmian (zmieniony od tamtej pory): ")) + target);
                continue;
            }
            std::wstring e;
            if (copyFileAtomic(backup, target, e)) {
                ++restored;
                if (log) log(std::wstring(tr(L"  restored: ", L"  przywrócono: ")) + target);
            } else {
                ++kept;
                if (log) log(std::wstring(tr(L"  could not restore ", L"  nie udało się przywrócić ")) + target + L": " + e);
            }
        } else if (kind == L"C") {
            if (!ours) {
                if (present) ++kept;
                continue;
            }
            if (DeleteFileW(target.c_str())) {
                ++removed;
                if (log) log(std::wstring(tr(L"  removed: ", L"  usunięto: ")) + target);
            } else {
                ++kept;
                if (log) log(std::wstring(tr(L"  could not remove ", L"  nie udało się usunąć ")) + target + L": " +
                             lastErrorText(GetLastError()));
            }
        }
    }
    writeFile(joinPath(run, L"undone.txt"), narrow(timestamp()));
    if (log)
        log(std::wstring(tr(L"Undone: ", L"Cofnięto: ")) + std::to_wstring(restored) + tr(L" restored, ", L" przywrócono, ") +
            std::to_wstring(removed) + tr(L" removed, ", L" usunięto, ") + std::to_wstring(kept) +
            tr(L" left as they are.", L" pozostawiono bez zmian."));
    return true;
}

// ------------------------------------------------------------------ the run

std::wstring zadigInstructions() {
    return tr(L"Zadig will start now; Windows will ask for administrator rights.\n\n"
              L"For an RTL-SDR dongle:\n"
              L"  1. Options > List All Devices.\n"
              L"  2. Choose \"Bulk-In, Interface (Interface 0)\" (or RTL2838UHIDIR / RTL2832U).\n"
              L"  3. The driver to the right of the arrow must be WinUSB.\n"
              L"  4. Click \"Replace Driver\" (or \"Install Driver\") and wait.\n"
              L"  5. Close Zadig. The radio is then checked again.\n\n"
              L"Only choose the radio: never a keyboard, mouse or other USB device.\n"
              L"The change can be undone in Device Manager (the device > Uninstall device).",
              L"Za chwilę uruchomi się Zadig; Windows poprosi o uprawnienia administratora.\n\n"
              L"Dla odbiornika RTL-SDR:\n"
              L"  1. Options > List All Devices.\n"
              L"  2. Wybierz \"Bulk-In, Interface (Interface 0)\" (albo RTL2838UHIDIR / RTL2832U).\n"
              L"  3. Sterownik po prawej stronie strzałki musi być WinUSB.\n"
              L"  4. Kliknij \"Replace Driver\" (albo \"Install Driver\") i zaczekaj.\n"
              L"  5. Zamknij Zadiga. Odbiornik zostanie wtedy sprawdzony ponownie.\n\n"
              L"Wybieraj tylko odbiornik: nigdy klawiaturę, mysz ani inne urządzenie USB.\n"
              L"Zmianę można cofnąć w Menedżerze urządzeń (urządzenie > Odinstaluj urządzenie).");
}

RunResult runPlan(const std::vector<PlanItem>& items, ActionContext& c) {
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    RunResult r;
    Transaction txn(c.backupsDir);
    for (const PlanItem& it : items) {
        if (!it.checked) continue;
        if (cancelled(c)) {
            ++r.skipped;
            continue;
        }

        // The one action that is not a catalogue package: install WinUSB for a radio.
        if (it.type == ActionType::InstallWinUsb) {
            say(c, L"> " + it.title);
            if (c.confirm && !c.confirm(tr(L"Install WinUSB driver", L"Instalacja sterownika WinUSB"),
                                        driverinst::consentText(it.deviceDesc))) {
                say(c, tr(L"  skipped", L"  pominięto"));
                ++r.skipped;
                continue;
            }
            driverinst::Request req;
            req.vid = it.vid;
            req.pid = it.pid;
            req.mi = it.mi;
            req.description = it.deviceDesc;
            req.interfaceGuid = it.interfaceGuid;
            req.workDir = joinPath(joinPath(c.cacheDir, L"winusb"),
                                   hex4(it.vid) + L"_" + hex4(it.pid) + L"_" + std::to_wstring(it.mi));
            const driverinst::Outcome o = driverinst::installWinUsb(req, [&c](const std::wstring& s) { say(c, s); });
            if (o.result == driverinst::Result::Installed) {
                say(c, tr(L"  the WinUSB driver is in place", L"  sterownik WinUSB jest na miejscu"));
                ++r.done;
            } else {
                say(c, L"  ! " + o.detail);
                ++r.failed;
            }
            if (c.progress) c.progress(0, 0);
            continue;
        }

        const Package* p = findPackage(it.packageId);
        if (!p) {
            ++r.failed;
            continue;
        }
        say(c, L"> " + it.title);
        std::wstring err, path;
        switch (it.type) {
            case ActionType::InstallWinUsb:
                break;  // handled above
            case ActionType::OpenPage:
                if (openUrl(p->url)) {
                    say(c, std::wstring(tr(L"  opened in the browser: ", L"  otwarto w przeglądarce: ")) + p->url);
                    ++r.done;
                } else {
                    say(c, std::wstring(tr(L"  could not open ", L"  nie udało się otworzyć ")) + p->url);
                    ++r.failed;
                }
                break;

            case ActionType::RunTool:
            case ActionType::RunInstaller: {
                if (!obtainPackage(*p, c, path, err)) {
                    say(c, L"  ! " + err);
                    ++r.failed;
                    break;
                }
                if (it.type == ActionType::RunTool && it.packageId == L"zadig" && c.confirm &&
                    !c.confirm(p->name, zadigInstructions())) {
                    say(c, tr(L"  skipped", L"  pominięto"));
                    ++r.skipped;
                    break;
                }
                say(c, std::wstring(tr(L"  starting ", L"  uruchamiam ")) + fileName(path) +
                           tr(L" -- it shows its own licence and questions", L" -- pokaże własną licencję i pytania"));
                DWORD code = 0;
                if (!launchAndWait(path, L"", code, err)) {
                    say(c, L"  ! " + err);
                    ++r.failed;
                    break;
                }
                say(c, L"  " + exitCodeText(*p, code));
                ++r.done;
                break;
            }

            case ActionType::CopyRtlSdr: {
                std::wstring dir;
                if (!obtainPackage(*p, c, path, err) || !unpackArchive(path, c, dir, err)) {
                    say(c, L"  ! " + err);
                    ++r.failed;
                    break;
                }
                const std::wstring dll = findRtlDll(dir, it.arch);
                if (dll.empty()) {
                    say(c, std::wstring(tr(L"  ! the package holds no ", L"  ! paczka nie zawiera ")) + archName(it.arch) +
                               L" rtlsdr.dll");
                    ++r.failed;
                    break;
                }
                // rtlsdr.dll and every DLL beside it in the package that it needs, directly or not.
                std::vector<std::wstring> files{dll};
                std::set<std::wstring> seen{lower(fileName(dll))};
                bool runtimeMissing = false;
                for (size_t i = 0; i < files.size(); ++i)
                    for (const std::wstring& imp : peImports(files[i])) {
                        const std::wstring li = lower(imp);
                        const std::wstring beside = joinPath(parentDir(dll), imp);
                        if (fileExists(beside)) {
                            if (seen.insert(li).second) files.push_back(beside);
                        } else if ((li.compare(0, 9, L"vcruntime") == 0 || li.compare(0, 5, L"msvcp") == 0 ||
                                    li.compare(0, 5, L"msvcr") == 0) &&
                                   !fileExists(joinPath(runtimeDirFor(it.arch), imp))) {
                            runtimeMissing = true;
                            say(c, std::wstring(tr(L"  note: it needs ", L"  uwaga: potrzebuje ")) + imp +
                                       tr(L", which is not on this computer -- install Microsoft Visual C++ "
                                          L"Redistributable (",
                                          L", którego nie ma na tym komputerze -- zainstaluj Microsoft Visual C++ "
                                          L"Redistributable (") +
                                       archName(it.arch) + L").");
                        }
                    }
                bool ok = true;
                for (const std::wstring& f : files) {
                    const std::wstring to = joinPath(it.targetDir, fileName(f));
                    bool changed = false;
                    if (!txn.install(f, to, changed, err)) {
                        say(c, L"  ! " + to + L": " + err);
                        ok = false;
                        break;
                    }
                    say(c, std::wstring(changed ? tr(L"  put in place: ", L"  wgrano: ")
                                                : tr(L"  already up to date: ", L"  już aktualny: ")) +
                               to);
                }
                if (ok) {
                    ++r.done;
                    if (runtimeMissing)
                        say(c, tr(L"  (the program will not load rtlsdr.dll until the runtime is installed)",
                                  L"  (program nie załaduje rtlsdr.dll, dopóki nie będzie środowiska uruchomieniowego)"));
                } else {
                    ++r.failed;
                }
                break;
            }
        }
        if (c.progress) c.progress(0, 0);
    }
    r.backupDir = txn.dir();
    if (txn.changes())
        say(c, std::wstring(tr(L"Backups and undo information: ", L"Kopie zapasowe i informacje do cofania: ")) + txn.dir());
    if (SUCCEEDED(com)) CoUninitialize();
    return r;
}

}  // namespace sdi

#include "util.hpp"


#include <bcrypt.h>
#include <shellapi.h>
#include <softpub.h>
#include <wincrypt.h>
#include <wintrust.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwctype>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) (((NTSTATUS)(s)) >= 0)
#endif

namespace sdi {

const wchar_t* archName(Arch a) {
    switch (a) {
        case Arch::X86: return L"x86";
        case Arch::X64: return L"x64";
        case Arch::Arm64: return L"ARM64";
        default: return L"?";
    }
}

// ---------------------------------------------------------------- strings

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n > 0 ? static_cast<size_t>(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? static_cast<size_t>(n) : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring lower(std::wstring s) {
    if (!s.empty()) CharLowerBuffW(&s[0], static_cast<DWORD>(s.size()));
    return s;
}

bool iequals(const std::wstring& a, const std::wstring& b) {
    return CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()), b.c_str(), static_cast<int>(b.size()), TRUE) ==
           CSTR_EQUAL;
}

bool istartsWith(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return CompareStringOrdinal(s.c_str(), static_cast<int>(prefix.size()), prefix.c_str(),
                                static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL;
}

bool wildcardMatch(const std::wstring& pattern, const std::wstring& name) {
    const std::wstring p = lower(pattern), s = lower(name);
    size_t pi = 0, si = 0, star = std::wstring::npos, mark = 0;
    while (si < s.size()) {
        if (pi < p.size() && (p[pi] == L'?' || p[pi] == s[si])) {
            ++pi;
            ++si;
        } else if (pi < p.size() && p[pi] == L'*') {
            star = pi++;
            mark = si;
        } else if (star != std::wstring::npos) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < p.size() && p[pi] == L'*') ++pi;
    return pi == p.size();
}

std::wstring trim(const std::wstring& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::iswspace(s[b])) ++b;
    while (e > b && std::iswspace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

std::wstring hex4(uint16_t v) {
    wchar_t b[8];
    swprintf(b, 8, L"%04X", static_cast<unsigned>(v));
    return b;
}

std::wstring formatBytes(uint64_t n) {
    wchar_t b[48];
    if (n < 1024)
        swprintf(b, 48, L"%llu B", static_cast<unsigned long long>(n));
    else if (n < (1ull << 20))
        swprintf(b, 48, L"%.1f KB", n / 1024.0);
    else if (n < (1ull << 30))
        swprintf(b, 48, L"%.1f MB", n / 1048576.0);
    else
        swprintf(b, 48, L"%.2f GB", n / 1073741824.0);
    return b;
}

std::wstring lastErrorText(DWORD e) {
    wchar_t* msg = nullptr;
    DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_IGNORE_INSERTS;
    HMODULE from = nullptr;
    if (e >= 12000 && e < 12200) from = GetModuleHandleW(L"winhttp.dll");  // WinHTTP's own codes
    flags |= from ? FORMAT_MESSAGE_FROM_HMODULE : FORMAT_MESSAGE_FROM_SYSTEM;
    FormatMessageW(flags, from, e, 0, reinterpret_cast<LPWSTR>(&msg), 0, nullptr);
    std::wstring text = msg ? trim(msg) : std::wstring();
    if (msg) LocalFree(msg);
    wchar_t code[32];
    swprintf(code, 32, L"(%lu)", static_cast<unsigned long>(e));
    return text.empty() ? std::wstring(L"error ") + code : text + L" " + code;
}

// ---------------------------------------------------------------- paths

std::wstring joinPath(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring fileName(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    return p == std::wstring::npos ? path : path.substr(p + 1);
}

std::wstring parentDir(const std::wstring& path) {
    const size_t p = path.find_last_of(L"\\/");
    if (p == std::wstring::npos) return {};
    if (p == 2 && path.size() > 1 && path[1] == L':') return path.substr(0, 3);  // "C:\"
    return path.substr(0, p);
}

std::wstring expandEnv(const std::wstring& s) {
    if (s.find(L'%') == std::wstring::npos) return s;
    const DWORD n = ExpandEnvironmentStringsW(s.c_str(), nullptr, 0);
    if (!n) return s;
    std::wstring out(n, L'\0');
    ExpandEnvironmentStringsW(s.c_str(), &out[0], n);
    out.resize(n - 1);
    return out;
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

bool dirExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

bool ensureDir(const std::wstring& p) {
    if (p.empty()) return false;
    if (dirExists(p)) return true;
    const std::wstring parent = parentDir(p);
    if (!parent.empty() && parent != p && !dirExists(parent) && !ensureDir(parent)) return false;
    return CreateDirectoryW(p.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool fileSize(const std::wstring& p, uint64_t& size) {
    WIN32_FILE_ATTRIBUTE_DATA d{};
    if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return false;
    size = (static_cast<uint64_t>(d.nFileSizeHigh) << 32) | d.nFileSizeLow;
    return true;
}

std::wstring knownFolder(const KNOWNFOLDERID& id) {
    PWSTR p = nullptr;
    std::wstring out;
    if (SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &p) == S_OK && p) out = p;
    if (p) CoTaskMemFree(p);
    return out;
}

std::wstring systemDir() {
    wchar_t b[MAX_PATH] = {};
    const UINT n = GetSystemDirectoryW(b, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(b) : std::wstring(L"C:\\Windows\\System32");
}

std::wstring systemWow64Dir() {
    wchar_t b[MAX_PATH] = {};
    const UINT n = GetSystemWow64DirectoryW(b, MAX_PATH);
    return n && n < MAX_PATH ? std::wstring(b) : std::wstring();
}

std::wstring moduleDir() {
    std::wstring b(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, &b[0], static_cast<DWORD>(b.size()));
    b.resize(n);
    return parentDir(b);
}

std::wstring dataDir() {
    std::wstring base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) base = expandEnv(L"%LOCALAPPDATA%");
    const std::wstring d = joinPath(base, L"SDRLoader");
    ensureDir(d);
    return d;
}

std::wstring timestamp() {
    SYSTEMTIME t;
    GetLocalTime(&t);
    wchar_t b[32];
    swprintf(b, 32, L"%04u%02u%02u-%02u%02u%02u", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    return b;
}

bool is64BitWindows() {
#if defined(_WIN64)
    return true;
#else
    BOOL wow = FALSE;
    return IsWow64Process(GetCurrentProcess(), &wow) && wow;
#endif
}

// ---------------------------------------------------------------- files

bool readFile(const std::wstring& p, std::string& out, uint64_t maxBytes) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && static_cast<uint64_t>(size.QuadPart) <= maxBytes;
    if (ok) {
        out.assign(static_cast<size_t>(size.QuadPart), '\0');
        size_t done = 0;
        while (ok && done < out.size()) {
            const DWORD want = static_cast<DWORD>(std::min<size_t>(out.size() - done, 1u << 24));
            DWORD got = 0;
            ok = ReadFile(h, &out[done], want, &got, nullptr) && got > 0;
            done += got;
        }
    }
    CloseHandle(h);
    return ok;
}

static bool writeAll(HANDLE h, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        const DWORD want = static_cast<DWORD>(std::min<size_t>(data.size() - done, 1u << 24));
        DWORD put = 0;
        if (!WriteFile(h, data.data() + done, want, &put, nullptr) || put == 0) return false;
        done += put;
    }
    return true;
}

bool writeFile(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = writeAll(h, data);
    CloseHandle(h);
    return ok;
}

bool appendFile(const std::wstring& p, const std::string& data) {
    HANDLE h = CreateFileW(p.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = writeAll(h, data);
    CloseHandle(h);
    return ok;
}

bool copyFileAtomic(const std::wstring& from, const std::wstring& to, std::wstring& err) {
    const std::wstring tmp = to + L".sdi-new";
    DeleteFileW(tmp.c_str());
    if (!CopyFileW(from.c_str(), tmp.c_str(), FALSE)) {
        err = lastErrorText(GetLastError());
        return false;
    }
    SetFileAttributesW(tmp.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (fileExists(to)) SetFileAttributesW(to.c_str(), FILE_ATTRIBUTE_NORMAL);
    if (!MoveFileExW(tmp.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD e = GetLastError();
        DeleteFileW(tmp.c_str());
        err = lastErrorText(e);
        if (e == ERROR_SHARING_VIOLATION || e == ERROR_ACCESS_DENIED || e == ERROR_USER_MAPPED_FILE)
            err += L" -- is the program that uses this file still running?";
        return false;
    }
    return true;
}

bool deleteTree(const std::wstring& dir) {
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(joinPath(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::wstring p = joinPath(dir, n);
            if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                deleteTree(p);
            } else if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(p.c_str());  // a link: remove the link, not what it points to
            } else {
                SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(p.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}

bool canWriteDir(const std::wstring& dir) {
    std::wstring d = dir;
    while (!d.empty() && !dirExists(d)) {
        const std::wstring p = parentDir(d);
        if (p == d) break;
        d = p;
    }
    if (d.empty() || !dirExists(d)) return false;
    wchar_t name[64];
    swprintf(name, 64, L".sdi-write-probe-%lu-%lu", GetCurrentProcessId(), GetTickCount());
    HANDLE h = CreateFileW(joinPath(d, name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                           FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

// ---------------------------------------------------------------- SHA-256

namespace {

class Sha256 {
public:
    Sha256() {
        if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0)))
            ok_ = NT_SUCCESS(BCryptCreateHash(alg_, &hash_, nullptr, 0, nullptr, 0, 0));
    }
    ~Sha256() {
        if (hash_) BCryptDestroyHash(hash_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    void update(const void* p, size_t n) {
        const auto* b = static_cast<const UCHAR*>(p);
        while (ok_ && n) {
            const ULONG chunk = static_cast<ULONG>(std::min<size_t>(n, 1u << 30));
            ok_ = NT_SUCCESS(BCryptHashData(hash_, const_cast<PUCHAR>(b), chunk, 0));
            b += chunk;
            n -= chunk;
        }
    }
    std::string hex() {
        UCHAR d[32] = {};
        if (!ok_ || !NT_SUCCESS(BCryptFinishHash(hash_, d, sizeof d, 0))) return {};
        static const char* digits = "0123456789abcdef";
        std::string s;
        for (UCHAR c : d) {
            s.push_back(digits[c >> 4]);
            s.push_back(digits[c & 15]);
        }
        return s;
    }
    bool ok() const { return ok_; }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE hash_ = nullptr;
    bool ok_ = false;
};

}  // namespace

std::string sha256Hex(const void* data, size_t n) {
    Sha256 h;
    h.update(data, n);
    return h.hex();
}

bool sha256File(const std::wstring& p, std::string& hex) {
    HANDLE f = CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                           FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    Sha256 h;
    std::string buf(1u << 20, '\0');
    bool ok = h.ok();
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(f, &buf[0], static_cast<DWORD>(buf.size()), &got, nullptr)) {
            ok = false;
            break;
        }
        if (!got) break;
        h.update(buf.data(), got);
    }
    CloseHandle(f);
    if (ok) hex = h.hex();
    return ok && !hex.empty();
}

// ---------------------------------------------------------------- PE files

namespace {

uint16_t le16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t le32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 | static_cast<uint32_t>(p[2]) << 16 |
           static_cast<uint32_t>(p[3]) << 24;
}

// Just enough of the PE format to answer two questions: what does it run as,
// and what does it import. Reads at offsets, so a big file costs nothing.
class Pe {
public:
    ~Pe() {
        if (h_ != INVALID_HANDLE_VALUE) CloseHandle(h_);
    }

    bool open(const std::wstring& path) {
        h_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h_ == INVALID_HANDLE_VALUE) return false;
        uint8_t dos[64];
        if (!readAt(0, dos, sizeof dos) || dos[0] != 'M' || dos[1] != 'Z') return false;
        const uint32_t lfanew = le32(dos + 0x3C);
        uint8_t fh[24];
        if (lfanew > (1u << 20) || !readAt(lfanew, fh, sizeof fh) || std::memcmp(fh, "PE\0\0", 4) != 0) return false;
        machine = le16(fh + 4);
        const uint16_t nsec = le16(fh + 6);
        optSize_ = le16(fh + 20);
        optOff_ = lfanew + 24;
        uint8_t m[2];
        if (optSize_ < 2 || !readAt(optOff_, m, 2)) return false;
        magic = le16(m);
        if (nsec > 96) return false;
        std::vector<uint8_t> st(nsec * 40u);
        if (nsec && !readAt(optOff_ + optSize_, st.data(), static_cast<DWORD>(st.size()))) return false;
        for (unsigned i = 0; i < nsec; ++i) {
            const uint8_t* s = &st[i * 40u];
            secs_.push_back({le32(s + 12), le32(s + 8), le32(s + 20), le32(s + 16)});
        }
        return true;
    }

    bool readAt(uint64_t off, void* buf, DWORD n) {
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(off);
        ov.OffsetHigh = static_cast<DWORD>(off >> 32);
        DWORD got = 0;
        return ReadFile(h_, buf, n, &got, &ov) && got == n;
    }

    // Data directory `i` (1 = imports, 14 = .NET header).
    bool dir(unsigned i, uint32_t& rva, uint32_t& size) {
        uint32_t countOff, dirsOff;
        if (magic == 0x10B) {
            countOff = 92;
            dirsOff = 96;
        } else if (magic == 0x20B) {
            countOff = 108;
            dirsOff = 112;
        } else {
            return false;
        }
        if (optSize_ < dirsOff + (i + 1) * 8) return false;
        uint8_t c[4], d[8];
        if (!readAt(optOff_ + countOff, c, 4) || i >= le32(c)) return false;
        if (!readAt(optOff_ + dirsOff + i * 8, d, 8)) return false;
        rva = le32(d);
        size = le32(d + 4);
        return rva != 0;
    }

    bool rvaToOff(uint32_t rva, uint64_t& off) const {
        for (const Sec& s : secs_) {
            const uint32_t span = std::max(s.vsize, s.rawsize);
            if (rva >= s.va && rva - s.va < span) {
                if (rva - s.va >= s.rawsize) return false;  // past what the file holds
                off = static_cast<uint64_t>(s.raw) + (rva - s.va);
                return true;
            }
        }
        return false;
    }

    bool cstring(uint32_t rva, std::string& out) {
        uint64_t off;
        if (!rvaToOff(rva, off)) return false;
        char buf[260] = {};
        OVERLAPPED ov{};
        ov.Offset = static_cast<DWORD>(off);
        ov.OffsetHigh = static_cast<DWORD>(off >> 32);
        DWORD got = 0;
        if (!ReadFile(h_, buf, sizeof buf - 1, &got, &ov) || !got) return false;
        const size_t n = strnlen(buf, got);
        if (n == got && got == sizeof buf - 1) return false;  // no end in sight: not a name
        out.assign(buf, n);
        return !out.empty();
    }

    uint16_t machine = 0, magic = 0;

private:
    struct Sec {
        uint32_t va, vsize, raw, rawsize;
    };
    HANDLE h_ = INVALID_HANDLE_VALUE;
    uint32_t optOff_ = 0;
    uint16_t optSize_ = 0;
    std::vector<Sec> secs_;
};

}  // namespace

Arch peArch(const std::wstring& path) {
    Pe pe;
    if (!pe.open(path)) return Arch::Unknown;
    switch (pe.machine) {
        case 0x8664: return Arch::X64;
        case 0xAA64: return Arch::Arm64;
        case 0x014C: {
            // A .NET program is marked i386 even when it is "Any CPU". Its own
            // header says whether it insists on 32 bits (required/preferred).
            uint32_t rva, size;
            uint64_t off;
            uint8_t cor[20];
            if (pe.magic == 0x10B && pe.dir(14, rva, size) && pe.rvaToOff(rva, off) && pe.readAt(off, cor, sizeof cor)) {
                const uint32_t flags = le32(cor + 16);
                const bool ilOnly = flags & 0x1, needs32 = flags & 0x2;
                if (ilOnly && !needs32) return is64BitWindows() ? Arch::X64 : Arch::X86;
            }
            return Arch::X86;
        }
        default: return Arch::Unknown;
    }
}

std::vector<std::wstring> peImports(const std::wstring& path) {
    std::vector<std::wstring> out;
    Pe pe;
    uint32_t rva, size;
    uint64_t off;
    if (!pe.open(path) || !pe.dir(1, rva, size) || !pe.rvaToOff(rva, off)) return out;
    for (unsigned i = 0; i < 1024; ++i) {
        uint8_t d[20];
        if (!pe.readAt(off + i * 20ull, d, sizeof d)) break;
        const uint32_t nameRva = le32(d + 12);
        if (!nameRva) break;  // the all-zero entry that ends the table
        std::string name;
        if (pe.cstring(nameRva, name)) out.push_back(widen(name));
    }
    return out;
}

bool authenticodeSigner(const std::wstring& path, std::wstring& signer, std::wstring& err) {
    signer.clear();
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct = sizeof fi;
    fi.pcwszFilePath = path.c_str();
    WINTRUST_DATA wd{};
    wd.cbStruct = sizeof wd;
    wd.dwUIChoice = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE;  // a revocation lookup can stall for minutes offline
    wd.dwUnionChoice = WTD_CHOICE_FILE;
    wd.pFile = &fi;
    wd.dwStateAction = WTD_STATEACTION_VERIFY;
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const HWND noUi = static_cast<HWND>(INVALID_HANDLE_VALUE);
    const LONG r = WinVerifyTrust(noUi, &action, &wd);
    bool ok = r == ERROR_SUCCESS;
    if (ok) {
        CRYPT_PROVIDER_DATA* pd = WTHelperProvDataFromStateData(wd.hWVTStateData);
        CRYPT_PROVIDER_SGNR* sg = pd ? WTHelperGetProvSignerFromChain(pd, 0, FALSE, 0) : nullptr;
        CRYPT_PROVIDER_CERT* pc = sg ? WTHelperGetProvCertFromChain(sg, 0) : nullptr;
        if (pc && pc->pCert) {
            wchar_t name[512] = {};
            CertGetNameStringW(pc->pCert, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name, 512);
            signer = name;
        }
        if (signer.empty()) {
            ok = false;
            err = L"signed, but the signer's name could not be read";
        }
    } else {
        wchar_t b[128];
        swprintf(b, 128, L"%ls (0x%08lX)", L"no valid digital signature",
                 static_cast<unsigned long>(r));
        err = b;
    }
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(noUi, &action, &wd);
    return ok;
}

// ---------------------------------------------------------------- processes

bool isElevated() {
    HANDLE t = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &t)) return false;
    TOKEN_ELEVATION e{};
    DWORD n = 0;
    const bool r = GetTokenInformation(t, TokenElevation, &e, sizeof e, &n) && e.TokenIsElevated;
    CloseHandle(t);
    return r;
}

bool runHidden(const std::wstring& exe, const std::wstring& cmdline, DWORD& exitCode, std::wstring& err) {
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cl = cmdline;
    if (!CreateProcessW(exe.c_str(), &cl[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        err = lastErrorText(GetLastError());
        return false;
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool launchAndWait(const std::wstring& file, const std::wstring& params, DWORD& exitCode, std::wstring& err) {
    const std::wstring dir = parentDir(file);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"open";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.empty() ? nullptr : params.c_str();
    sei.lpDirectory = dir.empty() ? nullptr : dir.c_str();
    sei.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&sei)) {
        const DWORD e = GetLastError();
        err = e == ERROR_CANCELLED ? L"cancelled: Windows asked for administrator rights and that was declined"
                                   : lastErrorText(e);
        return false;
    }
    exitCode = 0;
    if (sei.hProcess) {
        WaitForSingleObject(sei.hProcess, INFINITE);
        GetExitCodeProcess(sei.hProcess, &exitCode);
        CloseHandle(sei.hProcess);
    }
    return true;
}

bool openUrl(const std::wstring& url) {
    if (!istartsWith(url, L"https://")) return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

bool openFolder(const std::wstring& dir) {
    if (!dirExists(dir)) return false;
    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
}

std::wstring quoteArg(const std::wstring& a) {
    if (!a.empty() && a.find_first_of(L" \t\"") == std::wstring::npos) return a;
    std::wstring o = L"\"";
    size_t slashes = 0;
    for (wchar_t c : a) {
        if (c == L'\\') {
            ++slashes;
        } else if (c == L'"') {
            o.append(slashes * 2 + 1, L'\\');
            o.push_back(L'"');
            slashes = 0;
        } else {
            o.append(slashes, L'\\');
            o.push_back(c);
            slashes = 0;
        }
    }
    o.append(slashes * 2, L'\\');
    o.push_back(L'"');
    return o;
}

}  // namespace sdi

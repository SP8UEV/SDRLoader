// util.hpp -- small Windows helpers shared by the whole program.
//
// Everything here is plain Win32: strings, paths, files, SHA-256 (BCrypt),
// reading PE headers, Authenticode, starting processes. No third-party code.
#pragma once

#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <string>
#include <vector>

namespace sdi {

enum class Arch { Unknown, X86, X64, Arm64 };
const wchar_t* archName(Arch a);  // "x86", "x64", "ARM64", "?"

// ---------------------------------------------------------------- strings
std::wstring widen(const std::string& utf8);
std::string narrow(const std::wstring& w);  // to UTF-8
std::wstring lower(std::wstring s);
bool iequals(const std::wstring& a, const std::wstring& b);
bool istartsWith(const std::wstring& s, const std::wstring& prefix);
// Case-insensitive match with * and ? (for file names such as "ExtIO*RTL*.dll").
bool wildcardMatch(const std::wstring& pattern, const std::wstring& name);
std::wstring trim(const std::wstring& s);
std::wstring hex4(uint16_t v);  // "0BDA"
std::wstring formatBytes(uint64_t n);  // "1.3 MB"
std::wstring lastErrorText(DWORD e);

// ---------------------------------------------------------------- paths
std::wstring joinPath(const std::wstring& a, const std::wstring& b);
std::wstring fileName(const std::wstring& path);
std::wstring parentDir(const std::wstring& path);
std::wstring expandEnv(const std::wstring& s);
bool fileExists(const std::wstring& p);
bool dirExists(const std::wstring& p);
bool ensureDir(const std::wstring& p);  // creates missing parents too
bool fileSize(const std::wstring& p, uint64_t& size);
std::wstring knownFolder(const KNOWNFOLDERID& id);
std::wstring systemDir();       // the 64-bit System32 on 64-bit Windows
std::wstring systemWow64Dir();  // SysWOW64, or empty on 32-bit Windows
std::wstring moduleDir();       // the folder this program runs from
std::wstring dataDir();         // %LOCALAPPDATA%\SDRLoader, created
std::wstring timestamp();       // 20261002-193512
bool is64BitWindows();

// ---------------------------------------------------------------- files
bool readFile(const std::wstring& p, std::string& out, uint64_t maxBytes = 512ull << 20);
bool writeFile(const std::wstring& p, const std::string& data);
bool appendFile(const std::wstring& p, const std::string& data);
// Copy through a temporary file beside the target and an atomic rename, so a
// target is never left half-written.
bool copyFileAtomic(const std::wstring& from, const std::wstring& to, std::wstring& err);
bool deleteTree(const std::wstring& dir);
bool canWriteDir(const std::wstring& dir);  // tries it; walks up to an existing parent

// ---------------------------------------------------------------- binaries
std::string sha256Hex(const void* data, size_t n);
bool sha256File(const std::wstring& p, std::string& hex);
// The architecture a program or library runs as. A .NET "Any CPU" program runs
// as x64 on 64-bit Windows; one marked 32-bit-required/preferred as x86.
Arch peArch(const std::wstring& path);
// The DLLs a PE file imports by name (the ordinary import table), as written.
std::vector<std::wstring> peImports(const std::wstring& path);
// A valid Authenticode signature and the signer's name.
bool authenticodeSigner(const std::wstring& path, std::wstring& signer, std::wstring& err);

// ---------------------------------------------------------------- processes
bool isElevated();
// CreateProcess + wait. `cmdline` is the whole command line.
bool runHidden(const std::wstring& exe, const std::wstring& cmdline, DWORD& exitCode, std::wstring& err);
// ShellExecuteEx + wait: lets Windows ask for elevation when the program wants it.
bool launchAndWait(const std::wstring& file, const std::wstring& params, DWORD& exitCode, std::wstring& err);
bool openUrl(const std::wstring& url);       // https only
bool openFolder(const std::wstring& dir);
std::wstring quoteArg(const std::wstring& a);  // for a CreateProcess command line

}  // namespace sdi

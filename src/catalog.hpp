// catalog.hpp -- what this program knows about: packages, radios, programs.
//
// This is data, kept in one file so that adding a radio or an SDR program is a
// change here and nowhere else. Every package is pinned: a fixed release, its
// exact size and SHA-256 where the publisher records one, or the Authenticode
// signer it must carry. A newer release is a change to this table, never
// something the program picks up by itself.
#pragma once

#include "util.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace sdi {

enum class PkgKind {
    Archive,          // a .zip holding DLLs that are copied where a program looks
    Tool,             // a program to start (Zadig)
    VendorInstaller,  // the manufacturer's installer; it shows its own licence
    WebPage,          // only the publisher's page: the user downloads it there
};

// May the file be put in the offline package (payload\), next to this program?
enum class Payload {
    Never,              // the publisher's licence does not let us redistribute it: always fetched from them
    Allowed,            // open-source licence, and its complete source is shipped with it
    NeedsSourceReview,  // open-source licence, but the exact source of the prebuilt binary must be settled first
};

struct Package {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* publisher;
    PkgKind kind;
    const wchar_t* url;            // the publisher's own download (or page)
    const wchar_t* file;           // name in the cache and in payload\ (empty for a page)
    uint64_t size;                 // as published; 0 = not pinned
    const char* sha256;            // as published, lower-case hex; nullptr = not pinned
    const wchar_t* signer;         // the Authenticode signer it must carry; nullptr = not checked
    const wchar_t* licence;
    const wchar_t* source;         // where its licence and source are published
    const wchar_t* sourceArchive;  // the exact source to ship along in an offline package, or nullptr
    Payload payload;
};

// How a radio's USB driver is judged.
enum class DriverRule {
    NeedsWinUsb,     // must be on Windows' WinUSB driver (libusbK/libusb0 work for libusb-based programs)
    AnyDriver,       // any working driver will do: the manufacturer's package provides it
    NoDriverNeeded,  // Windows' own drivers (audio, HID) are enough
};

struct DeviceKind {
    const wchar_t* id;
    const wchar_t* name;
    uint16_t vid;
    std::vector<uint16_t> pids;  // empty: any product of this vendor ID
    DriverRule rule;
    int iface;                   // NeedsWinUsb: the interface that needs it (MI_xx), -1 = the device itself
    const wchar_t* fixPackage;   // the package that provides the driver, or nullptr
    const wchar_t* noteEn;
    const wchar_t* notePl;
};

// Where a program wants rtlsdr.dll.
enum class RtlPolicy {
    None,      // does not use rtlsdr.dll
    ExeDir,    // beside its .exe
    FixedDir,  // in a fixed folder (fixedDir)
    ShipsOwn,  // brings its own copy: reported, never touched
};

struct ProgramProfile {
    const wchar_t* id;
    const wchar_t* name;
    std::vector<const wchar_t*> exeNames;
    RtlPolicy rtl;
    const wchar_t* fixedDir;        // FixedDir: may contain %VARIABLES%
    Arch fixedArch;                 // FixedDir: the build it loads
    const wchar_t* markerDir;       // also "found" when this folder exists, even without the .exe
    const wchar_t* requireSibling;  // ExeDir: only when a file like this is beside the .exe
    const wchar_t* needsPackage;    // offered when the program is found and this is missing
    const wchar_t* noteEn;
    const wchar_t* notePl;
    // FixedDir: files beside the .exe the program loads BEFORE fixedDir. When one
    // is there, a copy in fixedDir would never be used, so none is proposed.
    std::vector<const wchar_t*> loadsFirst = {};
};

const std::vector<Package>& packages();
const Package* findPackage(const std::wstring& id);
const std::vector<DeviceKind>& deviceKinds();
const DeviceKind* matchDevice(uint16_t vid, uint16_t pid);
const std::vector<ProgramProfile>& programProfiles();
const ProgramProfile* profileForExe(const std::wstring& exeFileName);
const ProgramProfile* findProfile(const std::wstring& id);

const wchar_t* payloadName(Payload p);  // "never", "allowed", "needs-source-review"

}  // namespace sdi

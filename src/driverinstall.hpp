// driverinstall.hpp -- giving a USB radio Windows' WinUSB driver, the way Zadig
// does, but without Zadig's window: the program prepares a WinUSB driver package
// for the one device and installs it.
//
// The engine is libwdi (LGPL-2.1-or-later, https://github.com/pbatard/libwdi),
// the same library Zadig is built on. It builds a small INF for exactly this
// device, puts it in a catalogue, signs the catalogue with a self-signed
// certificate it generates, and asks Windows to install it. Installing that
// catalogue means that certificate is added to this machine's "Trusted Root"
// and "Trusted Publisher" stores, so a fresh self-signed driver is accepted --
// which is why this needs administrator rights AND the operator's clear say-so
// (consentText() below spells out exactly that, and nothing happens without a
// yes). This is the same thing Zadig does when you click "Install Driver".
//
// When libwdi is not compiled in (SDI_HAVE_LIBWDI undefined), available() is
// false and the planner offers Zadig instead; everything else still builds.
#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <string>

namespace sdi::driverinst {

// Is the automatic install compiled into this build?
bool available();

struct Request {
    uint16_t vid = 0, pid = 0;
    int mi = -1;                 // the composite interface to bind, or -1 for the whole device
    std::wstring description;    // for messages ("RTL2838UHIDIR")
    std::wstring interfaceGuid;  // the DeviceInterfaceGUID to give it (a fixed value is fine for libusb)
    HWND parent = nullptr;       // for any window libwdi shows
    std::wstring workDir;        // where the generated INF/cat/cert go (a fresh, writable folder)
};

enum class Result {
    Installed,       // the driver is in place
    NeedsElevation,  // must run as administrator
    NotCompiledIn,   // this build has no automatic install (use Zadig)
    DeviceNotFound,  // the device was not among those Windows lists now
    Failed,          // something else; `detail` says what
};

struct Outcome {
    Result result = Result::Failed;
    std::wstring detail;
};

// One line each, for the log.
Outcome installWinUsb(const Request& r, const std::function<void(const std::wstring&)>& log);

// What the operator must agree to before an automatic install runs. Shown by
// the GUI in a confirmation box; the install does not start without a yes.
std::wstring consentText(const std::wstring& deviceDescription);

}  // namespace sdi::driverinst

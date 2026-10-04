// planner.hpp -- from what was found to a list of proposed actions.
//
// The plan is only a proposal. Nothing in it happens until the user has seen
// it, ticked what they want and pressed Install; items that would replace an
// existing file start unticked.
#pragma once

#include "progscan.hpp"
#include "usbscan.hpp"

#include <string>
#include <vector>

namespace sdi {

enum class ActionType {
    InstallWinUsb,  // give the radio WinUSB automatically (libwdi), the way Zadig does
    RunInstaller,   // a manufacturer's installer (it shows its own licence)
    RunTool,        // Zadig
    CopyRtlSdr,     // rtlsdr.dll into a program's folder (backed up first when one is there)
    OpenPage,       // the publisher's download page
};

// A DeviceInterfaceGUID for a WinUSB install, fixed per radio type so repeated
// installs are stable; its value is otherwise arbitrary (libusb finds the radio
// by VID/PID, not by this GUID).
std::wstring winUsbGuid(uint16_t vid, uint16_t pid, int mi);

struct PlanItem {
    ActionType type = ActionType::OpenPage;
    std::wstring packageId;
    std::wstring targetDir;     // CopyRtlSdr
    std::wstring programName;   // CopyRtlSdr: for which program
    Arch arch = Arch::Unknown;  // CopyRtlSdr: which build
    bool checked = false;
    bool replaces = false;      // CopyRtlSdr: an rtlsdr.dll is already there
    bool needsAdmin = false;    // the item cannot run without administrator rights
    std::wstring title, reason;
    std::wstring key;        // unique within a plan
    std::wstring linkedKey;  // ticking this item ticks that one too (the runtime a DLL needs)
    // InstallWinUsb only:
    uint16_t vid = 0, pid = 0;
    int mi = -1;
    std::wstring interfaceGuid, deviceDesc;
};

struct SystemState {
    bool vcrtX64 = false, vcrtX86 = false;  // Microsoft's VCRUNTIME140.dll, per architecture
    bool sdrplayApi = false;
    std::wstring sdrplayApiDir;
};

SystemState probeSystem();

// SHA-256 of the rtlsdr.dll this program would put in place, per architecture,
// when the package is already at hand; empty when not known yet. A program
// that already has exactly that file gets no plan item.
struct RtlReference {
    std::string x86, x64;
};

std::vector<PlanItem> makePlan(const std::vector<DeviceFinding>& devices, const std::vector<ProgramFinding>& programs,
                               const SystemState& sys, const RtlReference& ref);

std::wstring packageActionTitle(const Package& p);

}  // namespace sdi

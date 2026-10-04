// usbscan.hpp -- which SDR radios are plugged in, and which driver Windows gave them.
//
// Read-only: SetupAPI lists the USB devices Windows knows to be present, with
// the service (driver) bound to each and any problem code. Composite devices
// appear as a parent plus one node per interface (USB\VID_x&PID_y&MI_00 ...).
#pragma once

#include "catalog.hpp"

#include <string>
#include <vector>

namespace sdi {

struct UsbNode {
    std::wstring instanceId, parentId, description, service;
    uint16_t vid = 0, pid = 0;
    int mi = -1;            // interface number of a composite device's child, -1 for a device
    unsigned long problem = 0;  // CM_PROB_*; 0 = none
};

enum class DevStatus {
    Ok,           // the right driver
    WorksLibusb,  // libusbK / libusb0: fine for libusb-based programs, not for WinUSB ones
    WrongDriver,  // some other driver, e.g. Realtek's DVB-T TV driver on an RTL dongle
    NoDriver,     // no driver, or a driver that failed
    Info,         // nothing to judge
};

struct DeviceFinding {
    const DeviceKind* kind = nullptr;
    std::wstring instanceId, description, service;
    DevStatus status = DevStatus::Info;
    // The exact node that should carry WinUSB (the composite interface for an
    // RTL dongle, the device itself for a HackRF): what an automatic install
    // targets. Set only when kind->rule == NeedsWinUsb.
    uint16_t targetVid = 0, targetPid = 0;
    int targetMi = -1;
};

// "USB\VID_0BDA&PID_2838&MI_00\7&1B2C..." -> 0x0BDA, 0x2838, 0
bool parseUsbId(const std::wstring& id, uint16_t& vid, uint16_t& pid, int& mi);

std::vector<UsbNode> enumerateUsb();
std::vector<DeviceFinding> evaluateDevices(const std::vector<UsbNode>& nodes);

const wchar_t* statusText(DevStatus s);
std::wstring adviceFor(const DeviceFinding& f);

}  // namespace sdi

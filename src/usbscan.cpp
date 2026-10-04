#include "usbscan.hpp"


#include <windows.h>
#include <cfgmgr32.h>
#include <setupapi.h>

namespace sdi {

namespace {

bool hexAt(const std::wstring& s, size_t pos, int digits, unsigned& out) {
    if (pos + digits > s.size()) return false;
    unsigned v = 0;
    for (int i = 0; i < digits; ++i) {
        const wchar_t c = s[pos + i];
        v <<= 4;
        if (c >= L'0' && c <= L'9')
            v |= static_cast<unsigned>(c - L'0');
        else if (c >= L'A' && c <= L'F')
            v |= static_cast<unsigned>(c - L'A' + 10);
        else
            return false;
    }
    out = v;
    return true;
}

std::wstring regString(HDEVINFO set, SP_DEVINFO_DATA& dd, DWORD prop) {
    wchar_t buf[1024] = {};
    DWORD type = 0, needed = 0;
    if (!SetupDiGetDeviceRegistryPropertyW(set, &dd, prop, &type, reinterpret_cast<PBYTE>(buf), sizeof buf - sizeof(wchar_t),
                                           &needed))
        return {};
    if (type != REG_SZ && type != REG_MULTI_SZ && type != REG_EXPAND_SZ) return {};
    return buf;  // for REG_MULTI_SZ: the first string
}

}  // namespace

bool parseUsbId(const std::wstring& id, uint16_t& vid, uint16_t& pid, int& mi) {
    // Only the device part: "USB\<this>\<instance>".
    const size_t a = id.find(L'\\');
    if (a == std::wstring::npos) return false;
    const size_t b = id.find(L'\\', a + 1);
    std::wstring seg = id.substr(a + 1, b == std::wstring::npos ? std::wstring::npos : b - a - 1);
    if (!seg.empty()) CharUpperBuffW(&seg[0], static_cast<DWORD>(seg.size()));
    unsigned v = 0, p = 0, m = 0;
    const size_t vp = seg.find(L"VID_"), pp = seg.find(L"PID_");
    if (vp == std::wstring::npos || pp == std::wstring::npos || !hexAt(seg, vp + 4, 4, v) || !hexAt(seg, pp + 4, 4, p))
        return false;
    vid = static_cast<uint16_t>(v);
    pid = static_cast<uint16_t>(p);
    const size_t mp = seg.find(L"MI_");
    mi = (mp != std::wstring::npos && hexAt(seg, mp + 3, 2, m)) ? static_cast<int>(m) : -1;
    return true;
}

std::vector<UsbNode> enumerateUsb() {
    std::vector<UsbNode> out;
    HDEVINFO set = SetupDiGetClassDevsW(nullptr, L"USB", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
    if (set == INVALID_HANDLE_VALUE) return out;
    SP_DEVINFO_DATA dd{};
    dd.cbSize = sizeof dd;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &dd); ++i) {
        wchar_t id[MAX_DEVICE_ID_LEN] = {};
        if (CM_Get_Device_IDW(dd.DevInst, id, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) continue;
        UsbNode n;
        n.instanceId = id;
        if (!parseUsbId(n.instanceId, n.vid, n.pid, n.mi)) continue;  // hubs, root hubs
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, dd.DevInst, 0) == CR_SUCCESS) {
            wchar_t pidBuf[MAX_DEVICE_ID_LEN] = {};
            if (CM_Get_Device_IDW(parent, pidBuf, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) n.parentId = pidBuf;
        }
        n.description = regString(set, dd, SPDRP_FRIENDLYNAME);
        if (n.description.empty()) n.description = regString(set, dd, SPDRP_DEVICEDESC);
        n.service = regString(set, dd, SPDRP_SERVICE);
        ULONG status = 0, problem = 0;
        if (CM_Get_DevNode_Status(&status, &problem, dd.DevInst, 0) == CR_SUCCESS && (status & DN_HAS_PROBLEM))
            n.problem = problem ? problem : 1;
        out.push_back(n);
    }
    SetupDiDestroyDeviceInfoList(set);
    return out;
}

std::vector<DeviceFinding> evaluateDevices(const std::vector<UsbNode>& nodes) {
    std::vector<DeviceFinding> out;
    for (const UsbNode& n : nodes) {
        if (n.mi >= 0) continue;  // interfaces are judged with their device
        const DeviceKind* k = matchDevice(n.vid, n.pid);
        if (!k) continue;
        std::vector<const UsbNode*> kids;
        for (const UsbNode& c : nodes)
            if (c.mi >= 0 && iequals(c.parentId, n.instanceId)) kids.push_back(&c);

        DeviceFinding f;
        f.kind = k;
        f.instanceId = n.instanceId;
        f.description = n.description;
        switch (k->rule) {
            case DriverRule::NeedsWinUsb: {
                const UsbNode* t = &n;
                if (k->iface >= 0)
                    for (const UsbNode* c : kids)
                        if (c->mi == k->iface) {
                            t = c;
                            break;
                        }
                f.service = t->service;
                if (t->problem || t->service.empty())
                    f.status = DevStatus::NoDriver;
                else if (iequals(t->service, L"WinUSB"))
                    f.status = DevStatus::Ok;
                else if (iequals(t->service, L"libusbK") || iequals(t->service, L"libusb0"))
                    f.status = DevStatus::WorksLibusb;
                else if (t == &n && iequals(t->service, L"usbccgp"))
                    f.status = DevStatus::NoDriver;  // the interface itself was not listed
                else
                    f.status = DevStatus::WrongDriver;
                if (t != &n && !t->description.empty()) f.description = t->description;
                // What an automatic WinUSB install would target: this very node.
                f.targetVid = t->vid;
                f.targetPid = t->pid;
                f.targetMi = t->mi;
                break;
            }
            case DriverRule::AnyDriver: {
                bool bad = n.problem || n.service.empty();
                std::wstring services;
                for (const UsbNode* c : kids) {
                    if (c->problem || c->service.empty()) bad = true;
                    if (!c->service.empty() && services.find(c->service) == std::wstring::npos)
                        services += (services.empty() ? L"" : L", ") + c->service;
                }
                f.service = iequals(n.service, L"usbccgp") && !services.empty() ? services : n.service;
                f.status = bad ? DevStatus::NoDriver : DevStatus::Ok;
                break;
            }
            case DriverRule::NoDriverNeeded:
                f.service = n.service;
                f.status = DevStatus::Info;
                break;
        }
        out.push_back(f);
    }
    return out;
}

const wchar_t* statusText(DevStatus s) {
    switch (s) {
        case DevStatus::Ok: return L"OK";
        case DevStatus::WorksLibusb: return L"Works (libusb)";
        case DevStatus::WrongDriver: return L"Wrong driver";
        case DevStatus::NoDriver: return L"No driver";
        default: return L"Nothing needed";
    }
}

std::wstring adviceFor(const DeviceFinding& f) {
    const DeviceKind& k = *f.kind;
    const std::wstring note = k.note;
    const Package* fix = k.fixPackage ? findPackage(k.fixPackage) : nullptr;
    switch (f.status) {
        case DevStatus::Ok:
        case DevStatus::Info: return note;
        case DevStatus::WorksLibusb:
            return std::wstring(L"Driver ") + f.service +
                   L" works with programs built on libusb. Programs that use WinUSB directly need WinUSB -- "
                      L"Zadig can switch it.";
        case DevStatus::WrongDriver:
            return std::wstring(L"Windows has given it the \"") + f.service +
                   L"\" driver (often the DVB-T TV driver). SDR programs need WinUSB -- Zadig replaces it, and "
                      L"Device Manager can roll it back.";
        case DevStatus::NoDriver:
            if (fix) return std::wstring(L"Install: ") + fix->name + L". " + note;
            return note;
    }
    return note;
}

}  // namespace sdi

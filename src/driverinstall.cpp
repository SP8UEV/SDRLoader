#include "driverinstall.hpp"

#include "util.hpp"

#ifdef SDI_HAVE_LIBWDI
extern "C" {
#include <libwdi.h>
}
#endif

namespace sdi::driverinst {

std::wstring consentText(const std::wstring& deviceDescription) {
    const std::wstring dev =
        deviceDescription.empty() ? std::wstring(L"the selected radio") : deviceDescription;
    return std::wstring(L"About to install Windows' WinUSB driver for ") +
           dev +
           L".\n\n"
              L"This is the same step as Zadig's \"Install Driver\". To do it, the program:\n"
              L"  - builds a small driver package (an INF) for this one radio;\n"
              L"  - generates a certificate and signs that package with it;\n"
              L"  - adds that certificate to this computer's trusted-certificate stores, so Windows\n"
              L"    accepts the driver;\n"
              L"  - asks Windows to bind WinUSB to the radio.\n\n"
              L"It needs administrator rights, and it changes ONLY the driver of this radio. It can be\n"
              L"undone in Device Manager (the device > Uninstall device > remove driver), and the\n"
              L"certificate can be removed with certmgr.msc.\n\n"
              L"Install the WinUSB driver now?";
}

#ifdef SDI_HAVE_LIBWDI

bool available() { return true; }

namespace {

// libwdi speaks UTF-8 char*; narrow() already produces UTF-8.
std::string u8(const std::wstring& w) { return narrow(w); }

}  // namespace

Outcome installWinUsb(const Request& r, const std::function<void(const std::wstring&)>& log) {
    auto say = [&](const std::wstring& s) {
        if (log) log(s);
    };
    if (!isElevated()) return {Result::NeedsElevation, L"administrator rights are needed"};

    struct wdi_device_info* list = nullptr;
    struct wdi_options_create_list lc{};
    lc.list_all = true;
    lc.list_hubs = false;
    lc.trim_whitespaces = true;
    int rc = wdi_create_list(&list, &lc);
    if (rc != WDI_SUCCESS)
        return {Result::Failed, std::wstring(L"the USB device list could not be read: ") +
                                    widen(wdi_strerror(rc))};

    struct wdi_device_info* match = nullptr;
    for (struct wdi_device_info* d = list; d; d = d->next) {
        if (d->vid != r.vid || d->pid != r.pid) continue;
        const int dmi = d->is_composite ? d->mi : -1;
        if (r.mi >= 0 ? dmi == r.mi : true) {
            match = d;
            break;
        }
    }
    if (!match) {
        wdi_destroy_list(list);
        return {Result::DeviceNotFound, L"the radio was not among the USB devices Windows lists now"};
    }
    say(std::wstring(L"  device: ") +
        (match->desc ? widen(match->desc) : r.description) + L" (" +
        (match->hardware_id ? widen(match->hardware_id) : std::wstring()) + L")");

    if (!ensureDir(r.workDir)) {
        wdi_destroy_list(list);
        return {Result::Failed, std::wstring(L"cannot create the folder ") + r.workDir};
    }

    std::string workDir = u8(r.workDir);
    std::string guid = u8(r.interfaceGuid);
    std::string vendor = u8(L"SDRLoader");
    std::string cert = "CN=SDRLoader (self-signed driver certificate)";
    const char* inf = "sdr_winusb.inf";

    struct wdi_options_prepare_driver pd{};
    pd.driver_type = WDI_WINUSB;
    pd.vendor_name = vendor.empty() ? nullptr : &vendor[0];
    pd.device_guid = guid.empty() ? nullptr : &guid[0];
    pd.cert_subject = &cert[0];
    pd.disable_cat = false;      // we WANT a catalogue, so the install is silent
    pd.disable_signing = false;  // and it signed, by the generated certificate
    say(L"  preparing the WinUSB driver package...");
    rc = wdi_prepare_driver(match, workDir.c_str(), inf, &pd);
    if (rc != WDI_SUCCESS) {
        wdi_destroy_list(list);
        return {Result::Failed, std::wstring(L"the driver package could not be prepared: ") +
                                    widen(wdi_strerror(rc))};
    }

    struct wdi_options_install_driver id{};
    id.hWnd = r.parent;
    say(L"  installing (Windows may show a progress window)...");
    rc = wdi_install_driver(match, workDir.c_str(), inf, &id);
    wdi_destroy_list(list);
    if (rc != WDI_SUCCESS)
        return {Result::Failed, std::wstring(L"the driver did not install: ") +
                                    widen(wdi_strerror(rc))};
    return {Result::Installed, {}};
}

#else  // no libwdi in this build

bool available() { return false; }

Outcome installWinUsb(const Request&, const std::function<void(const std::wstring&)>&) {
    return {Result::NotCompiledIn, L"this build cannot install drivers automatically; use Zadig"};
}

#endif

}  // namespace sdi::driverinst

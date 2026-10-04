#include "driverinstall.hpp"

#include "i18n.hpp"
#include "util.hpp"

#ifdef SDI_HAVE_LIBWDI
extern "C" {
#include <libwdi.h>
}
#endif

namespace sdi::driverinst {

std::wstring consentText(const std::wstring& deviceDescription) {
    const std::wstring dev =
        deviceDescription.empty() ? std::wstring(tr(L"the selected radio", L"wybranego odbiornika")) : deviceDescription;
    return std::wstring(tr(L"About to install Windows' WinUSB driver for ", L"Za chwilę zostanie zainstalowany sterownik WinUSB systemu Windows dla ")) +
           dev +
           tr(L".\n\n"
              L"This is the same step as Zadig's \"Install Driver\". To do it, the program:\n"
              L"  - builds a small driver package (an INF) for this one radio;\n"
              L"  - generates a certificate and signs that package with it;\n"
              L"  - adds that certificate to this computer's trusted-certificate stores, so Windows\n"
              L"    accepts the driver;\n"
              L"  - asks Windows to bind WinUSB to the radio.\n\n"
              L"It needs administrator rights, and it changes ONLY the driver of this radio. It can be\n"
              L"undone in Device Manager (the device > Uninstall device > remove driver), and the\n"
              L"certificate can be removed with certmgr.msc.\n\n"
              L"Install the WinUSB driver now?",
              L".\n\n"
              L"To ten sam krok co \"Install Driver\" w Zadigu. Aby go wykonać, program:\n"
              L"  - tworzy mały pakiet sterownika (plik INF) dla tego jednego odbiornika;\n"
              L"  - generuje certyfikat i nim podpisuje ten pakiet;\n"
              L"  - dodaje ten certyfikat do magazynów zaufanych certyfikatów tego komputera, aby\n"
              L"    Windows zaakceptował sterownik;\n"
              L"  - prosi Windows o powiązanie WinUSB z odbiornikiem.\n\n"
              L"Wymaga uprawnień administratora i zmienia WYŁĄCZNIE sterownik tego odbiornika. Można\n"
              L"to cofnąć w Menedżerze urządzeń (urządzenie > Odinstaluj urządzenie > usuń sterownik),\n"
              L"a certyfikat usunąć w certmgr.msc.\n\n"
              L"Zainstalować teraz sterownik WinUSB?");
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
    if (!isElevated()) return {Result::NeedsElevation, tr(L"administrator rights are needed", L"potrzebne są uprawnienia administratora")};

    struct wdi_device_info* list = nullptr;
    struct wdi_options_create_list lc{};
    lc.list_all = true;
    lc.list_hubs = false;
    lc.trim_whitespaces = true;
    int rc = wdi_create_list(&list, &lc);
    if (rc != WDI_SUCCESS)
        return {Result::Failed, std::wstring(tr(L"the USB device list could not be read: ", L"nie udało się odczytać listy urządzeń USB: ")) +
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
        return {Result::DeviceNotFound, tr(L"the radio was not among the USB devices Windows lists now",
                                           L"odbiornika nie ma wśród urządzeń USB wymienianych teraz przez Windows")};
    }
    say(std::wstring(tr(L"  device: ", L"  urządzenie: ")) +
        (match->desc ? widen(match->desc) : r.description) + L" (" +
        (match->hardware_id ? widen(match->hardware_id) : std::wstring()) + L")");

    if (!ensureDir(r.workDir)) {
        wdi_destroy_list(list);
        return {Result::Failed, std::wstring(tr(L"cannot create the folder ", L"nie da się utworzyć folderu ")) + r.workDir};
    }

    std::string workDir = u8(r.workDir);
    std::string guid = u8(r.interfaceGuid);
    std::string vendor = u8(tr(L"SDRLoader", L"SDRLoader"));
    std::string cert = "CN=SDRLoader (self-signed driver certificate)";
    const char* inf = "sdr_winusb.inf";

    struct wdi_options_prepare_driver pd{};
    pd.driver_type = WDI_WINUSB;
    pd.vendor_name = vendor.empty() ? nullptr : &vendor[0];
    pd.device_guid = guid.empty() ? nullptr : &guid[0];
    pd.cert_subject = &cert[0];
    pd.disable_cat = false;      // we WANT a catalogue, so the install is silent
    pd.disable_signing = false;  // and it signed, by the generated certificate
    say(tr(L"  preparing the WinUSB driver package...", L"  przygotowuję pakiet sterownika WinUSB..."));
    rc = wdi_prepare_driver(match, workDir.c_str(), inf, &pd);
    if (rc != WDI_SUCCESS) {
        wdi_destroy_list(list);
        return {Result::Failed, std::wstring(tr(L"the driver package could not be prepared: ", L"nie udało się przygotować pakietu sterownika: ")) +
                                    widen(wdi_strerror(rc))};
    }

    struct wdi_options_install_driver id{};
    id.hWnd = r.parent;
    say(tr(L"  installing (Windows may show a progress window)...", L"  instaluję (Windows może pokazać okno postępu)..."));
    rc = wdi_install_driver(match, workDir.c_str(), inf, &id);
    wdi_destroy_list(list);
    if (rc != WDI_SUCCESS)
        return {Result::Failed, std::wstring(tr(L"the driver did not install: ", L"sterownik nie zainstalował się: ")) +
                                    widen(wdi_strerror(rc))};
    return {Result::Installed, {}};
}

#else  // no libwdi in this build

bool available() { return false; }

Outcome installWinUsb(const Request&, const std::function<void(const std::wstring&)>&) {
    return {Result::NotCompiledIn, tr(L"this build cannot install drivers automatically; use Zadig",
                                      L"ta wersja nie instaluje sterowników automatycznie; użyj Zadiga")};
}

#endif

}  // namespace sdi::driverinst

#include "catalog.hpp"

namespace sdi {

// ------------------------------------------------------------------ packages
//
// Sizes and SHA-256 values are the ones GitHub records for each release file
// (api.github.com/repos/<owner>/<repo>/releases/tags/<tag>, "digest"). Where a
// release predates those digests, the file's Authenticode signer is checked
// instead. Checked on 2026-10-02.

const std::vector<Package>& packages() {
    static const std::vector<Package> p = {
        {L"rtlsdr-blog", L"RTL-SDR Blog drivers V1.4.0 (rtlsdr.dll, 32- and 64-bit)", L"RTL-SDR Blog",
         PkgKind::Archive, L"https://github.com/rtlsdrblog/rtl-sdr-blog/releases/download/V1.4.0/Release.zip",
         L"rtl-sdr-blog-V1.4.0.zip", 1328379, "7ef33f1304647f65e5e0fde43637a73d54f076e91e651a3cecc4f55a17fd9815",
         nullptr,
         L"GPL-2.0-or-later (librtlsdr); LGPL-2.1 (libusb, built into rtlsdr.dll; pthreadVC2.dll); "
         L"Microsoft redistributable (msvcr100.dll)",
         L"https://github.com/rtlsdrblog/rtl-sdr-blog/tree/V1.4.0",
         L"https://github.com/rtlsdrblog/rtl-sdr-blog/archive/refs/tags/V1.4.0.zip", Payload::NeedsSourceReview},

        {L"zadig", L"Zadig 2.9 (installs Windows' WinUSB driver for a USB radio)", L"Akeo Consulting (Pete Batard)",
         PkgKind::Tool, L"https://github.com/pbatard/libwdi/releases/download/v1.5.1/zadig-2.9.exe", L"zadig-2.9.exe",
         5334088, nullptr, L"Akeo Consulting", L"GPL-3.0-or-later (Zadig); LGPL-3.0 (libwdi)",
         L"https://github.com/pbatard/libwdi/tree/v1.5.1",
         L"https://github.com/pbatard/libwdi/archive/refs/tags/v1.5.1.zip", Payload::Allowed},

        {L"vcredist-x64", L"Microsoft Visual C++ Redistributable (x64)", L"Microsoft", PkgKind::VendorInstaller,
         L"https://aka.ms/vs/17/release/vc_redist.x64.exe", L"vc_redist.x64.exe", 0, nullptr, L"Microsoft Corporation",
         L"Microsoft Software License Terms (shown by the installer)",
         L"https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist", nullptr, Payload::Never},

        {L"vcredist-x86", L"Microsoft Visual C++ Redistributable (x86)", L"Microsoft", PkgKind::VendorInstaller,
         L"https://aka.ms/vs/17/release/vc_redist.x86.exe", L"vc_redist.x86.exe", 0, nullptr, L"Microsoft Corporation",
         L"Microsoft Software License Terms (shown by the installer)",
         L"https://learn.microsoft.com/cpp/windows/latest-supported-vc-redist", nullptr, Payload::Never},

        {L"bladerf", L"bladeRF for Windows 2025.10 (USB driver, libbladeRF 2.6.0, FPGA images)", L"Nuand",
         PkgKind::VendorInstaller,
         L"https://github.com/Nuand/bladeRF/releases/download/2025.10/bladeRF-win-installer-2025.10.exe",
         L"bladeRF-win-installer-2025.10.exe", 31305112,
         "8682d4a3a7e762f0a3f40e85ba8d0ea8c70e7d21537d58c71d5f6fd5262c3cfe", nullptr,
         L"Nuand's terms (shown by the installer); libbladeRF: LGPL-2.1", L"https://github.com/Nuand/bladeRF", nullptr,
         Payload::Never},

        {L"pluto", L"PlutoSDR / M2k USB drivers v0.9", L"Analog Devices", PkgKind::VendorInstaller,
         L"https://github.com/analogdevicesinc/plutosdr-m2k-drivers-win/releases/download/v0.9/"
         L"PlutoSDR-M2k-USB-Drivers.exe",
         L"PlutoSDR-M2k-USB-Drivers-v0.9.exe", 6729208, nullptr, L"Analog Devices",
         L"Analog Devices' terms (shown by the installer)",
         L"https://github.com/analogdevicesinc/plutosdr-m2k-drivers-win", nullptr, Payload::Never},

        {L"sdrplay-api", L"SDRplay API 3.x (includes the RSP USB driver)", L"SDRplay", PkgKind::WebPage,
         L"https://www.sdrplay.com/api/", L"", 0, nullptr, nullptr,
         L"SDRplay's licence (proprietary; accepted on their site and in their installer)",
         L"https://www.sdrplay.com/api/", nullptr, Payload::Never},

        {L"pothossdr", L"PothosSDR (SoapySDR with modules for LimeSDR, USRP, Airspy HF+ and others)",
         L"Pothosware / MyriadRF", PkgKind::WebPage, L"https://downloads.myriadrf.org/builds/PothosSDR/", L"", 0,
         nullptr, nullptr, L"Open source, several licences (SoapySDR: BSL-1.0; bundled tools: GPL and others)",
         L"https://github.com/pothosware/PothosSDR", nullptr, Payload::Never},

        {L"ftdi-d3xx", L"FTDI D3XX driver (for the LimeSDR Mini's FT601 USB bridge)", L"FTDI", PkgKind::WebPage,
         L"https://ftdichip.com/drivers/d3xx-drivers/", L"", 0, nullptr, nullptr,
         L"FTDI's driver licence (shown on their site and in the package)",
         L"https://ftdichip.com/drivers/d3xx-drivers/", nullptr, Payload::Never},
    };
    return p;
}

const Package* findPackage(const std::wstring& id) {
    for (const Package& p : packages())
        if (id == p.id) return &p;
    return nullptr;
}

// ------------------------------------------------------------------ radios

const std::vector<DeviceKind>& deviceKinds() {
    static const std::vector<DeviceKind> d = {
        {L"rtlsdr", L"RTL-SDR (RTL2832U)", 0x0BDA, {0x2838, 0x2832}, DriverRule::NeedsWinUsb, 0, L"zadig",
         L"Needs WinUSB on interface 0 -- in Zadig: \"Bulk-In, Interface (Interface 0)\"."},
        {L"hackrf", L"HackRF One", 0x1D50, {0x6089}, DriverRule::NeedsWinUsb, -1, L"zadig",
         L"Windows 8 and later give it WinUSB by themselves (WCID)."},
        {L"airspy", L"Airspy R2 / Mini", 0x1D50, {0x60A1}, DriverRule::NeedsWinUsb, -1, L"zadig",
         L"Windows 8 and later give it WinUSB by themselves (WCID)."},
        {L"airspyhf", L"Airspy HF+ / HF+ Discovery", 0x03EB, {0x800C}, DriverRule::NeedsWinUsb, -1, L"zadig",
         L"Normally gets WinUSB by itself."},
        {L"sdrplay", L"SDRplay RSP", 0x1DF7, {}, DriverRule::AnyDriver, -1, L"sdrplay-api",
         L"Its USB driver comes with SDRplay's API installer."},
        {L"bladerf", L"Nuand bladeRF", 0x2CF0, {}, DriverRule::AnyDriver, -1, L"bladerf",
         L"Driver, library and FPGA images come with Nuand's installer."},
        {L"pluto", L"ADALM-PLUTO (PlutoSDR)", 0x0456, {0xB673, 0xB674}, DriverRule::AnyDriver, -1, L"pluto",
         L"Analog Devices' drivers make it appear as a network adapter (192.168.2.1)."},
        {L"limesdr-usb", L"LimeSDR-USB", 0x1D50, {0x6108}, DriverRule::NeedsWinUsb, -1, L"zadig",
         L"Uses WinUSB (through libusb, for LimeSuite / SoapySDR). Install LimeSuite to use it."},
        {L"limesdr-mini", L"LimeSDR Mini / Mini 2 (FTDI FT601)", 0x0403, {0x601F}, DriverRule::AnyDriver, -1,
         L"ftdi-d3xx",
         L"Its FT601 bridge needs FTDI's D3XX driver -- WinUSB cannot drive it. LimeSuite bundles the driver too."},
        {L"usrp", L"Ettus USRP B200 / B210", 0x2500, {}, DriverRule::NeedsWinUsb, -1, L"zadig",
         L"The B200/B210 use WinUSB. Install UHD to use them; networked USRPs (N/X series) need no driver."},
        {L"funcube", L"FUNcube Dongle Pro / Pro+", 0x04D8, {0xFB56, 0xFB31}, DriverRule::NoDriverNeeded, -1, nullptr,
         L"Uses Windows' own audio and HID drivers -- nothing to install."},
    };
    return d;
}

const DeviceKind* matchDevice(uint16_t vid, uint16_t pid) {
    for (const DeviceKind& k : deviceKinds()) {
        if (k.vid != vid) continue;
        if (k.pids.empty()) return &k;
        for (uint16_t p : k.pids)
            if (p == pid) return &k;
    }
    return nullptr;
}

// ------------------------------------------------------------------ programs

const std::vector<ProgramProfile>& programProfiles() {
    static const std::vector<ProgramProfile> p = {
        {L"chameleonsdr", L"ChameleonSDR", {L"ChameleonSDR.exe"}, RtlPolicy::FixedDir,
         L"%APPDATA%\\C-SDR\\PUT_SDR_FILES_HERE", Arch::X64, L"%APPDATA%\\C-SDR", nullptr, nullptr,
         L"Loads a 64-bit rtlsdr.dll from its PUT_SDR_FILES_HERE folder. HackRF, Airspy, SDRplay, bladeRF and "
         L"PlutoSDR need only their USB driver.",
         {L"third_party\\rtlsdr\\x64\\rtlsdr.dll", L"rtlsdr.dll"}},
        {L"sdrsharp", L"SDR# (SDRSharp)", {L"SDRSharp.exe", L"SDRSharp.dotnet8.exe", L"SDRSharp.dotnet9.exe"},
         RtlPolicy::ExeDir, nullptr, Arch::Unknown, nullptr, nullptr, nullptr,
         L"Loads rtlsdr.dll from its own folder (the RTL-SDR Blog V4 needs the Blog build)."},
        {L"hdsdr", L"HDSDR", {L"HDSDR.exe"}, RtlPolicy::ExeDir, nullptr, Arch::Unknown, nullptr, L"ExtIO*RTL*.dll",
         nullptr, L"Its ExtIO_RTL plug-in needs rtlsdr.dll beside HDSDR.exe."},
        {L"sdrpp", L"SDR++", {L"sdrpp.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown, nullptr, nullptr, nullptr,
         L"Ships its own drivers -- left as it is."},
        {L"sdrconsole", L"SDR Console", {L"SDR Console.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown, nullptr,
         nullptr, nullptr, L"Ships its own drivers -- left as it is."},
        {L"cubicsdr", L"CubicSDR", {L"CubicSDR.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown, nullptr, nullptr,
         nullptr, L"Ships SoapySDR and its modules -- left as it is."},
        {L"sdrangel", L"SDRangel", {L"sdrangel.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown, nullptr, nullptr,
         nullptr, L"Ships its own drivers -- left as it is."},
        {L"satdump", L"SatDump", {L"satdump-ui.exe", L"satdump.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown,
         nullptr, nullptr, nullptr, L"Ships its own drivers -- left as it is."},
        {L"gqrx", L"Gqrx", {L"gqrx.exe"}, RtlPolicy::ShipsOwn, nullptr, Arch::Unknown, nullptr, nullptr, nullptr,
         L"Ships its own drivers -- left as it is."},
        {L"sdruno", L"SDRuno", {L"SDRuno.exe"}, RtlPolicy::None, nullptr, Arch::Unknown, nullptr, nullptr,
         L"sdrplay-api", L"Talks to RSPs through the SDRplay API."},
        {L"sdrconnect", L"SDRconnect", {L"SDRconnect.exe"}, RtlPolicy::None, nullptr, Arch::Unknown, nullptr,
         nullptr, nullptr, L"SDRplay's program."},
    };
    return p;
}

const ProgramProfile* profileForExe(const std::wstring& exeFileName) {
    for (const ProgramProfile& p : programProfiles())
        for (const wchar_t* e : p.exeNames)
            if (iequals(exeFileName, e)) return &p;
    return nullptr;
}

const ProgramProfile* findProfile(const std::wstring& id) {
    for (const ProgramProfile& p : programProfiles())
        if (id == p.id) return &p;
    return nullptr;
}

const wchar_t* payloadName(Payload p) {
    switch (p) {
        case Payload::Allowed: return L"allowed";
        case Payload::NeedsSourceReview: return L"needs-source-review";
        default: return L"never";
    }
}

}  // namespace sdi

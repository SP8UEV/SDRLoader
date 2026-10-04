# Third-party software

SDRLoader's own code is under the MIT licence (see `LICENSE`). It
contains **no third-party code**. It can *fetch, check and install* the
packages below. Each one stays under its own licence, and each comes from
its publisher's own address. The exact release, size and SHA-256 (or the
publisher's digital signature) are pinned in `src/catalog.cpp`.

## Linked library: libwdi (optional)

When built with `-DSDI_WITH_LIBWDI=ON`, the program links **libwdi**
(LGPL-2.1-or-later, https://github.com/pbatard/libwdi) to install WinUSB
automatically — the same library Zadig is built on. This is a *linked
library*, not a fetched file, so the LGPL applies to the distributed binary:
anyone you give the program to must be able to replace libwdi with their own
build (ship libwdi's source, or your changes, and keep the program relinkable).
The default build does not include libwdi, and is MIT-only.

libwdi installs a WinUSB driver package that it signs with a **self-signed
certificate it generates**; installing it places that certificate in the
machine's Trusted Root and Trusted Publisher stores. The program asks the user
to confirm this explicitly first (`driverinst::consentText`). WinUSB itself is
Microsoft's, and ships inside Windows.

| Package | Publisher | Licence | How the program gets it | Offline package |
|---|---|---|---|---|
| RTL-SDR Blog drivers V1.4.0 (`rtlsdr.dll`, x86 + x64) | RTL-SDR Blog | GPL-2.0-or-later (librtlsdr); LGPL-2.1 (libusb, built into `rtlsdr.dll`; `pthreadVC2.dll`); Microsoft redistributable (`msvcr100.dll`) | Download from GitHub release `V1.4.0`, SHA-256 checked | **Needs source review** (see below) |
| Zadig 2.9 | Akeo Consulting (Pete Batard) | GPL-3.0-or-later (Zadig), LGPL-3.0 (libwdi) | Download from GitHub release `v1.5.1`, signature "Akeo Consulting" checked | Allowed, with the libwdi `v1.5.1` source |
| Microsoft Visual C++ Redistributable (x64, x86) | Microsoft | Microsoft Software License Terms (shown by the installer) | Download from `aka.ms`, signature "Microsoft Corporation" checked | Never |
| bladeRF for Windows 2025.10 | Nuand | Nuand's terms (shown by the installer); libbladeRF LGPL-2.1 | Download from GitHub release `2025.10`, SHA-256 checked | Never |
| PlutoSDR / M2k USB drivers v0.9 | Analog Devices | Analog Devices' terms (shown by the installer) | Download from GitHub release `v0.9`, signature "Analog Devices" checked | Never |
| SDRplay API 3.x | SDRplay | Proprietary | Only the publisher's page is opened; the user downloads it there | Never |
| PothosSDR (SoapySDR and modules) | Pothosware / MyriadRF | Several open-source licences | Only the publisher's page is opened | Never |

## What "offline package" means, and what it obliges

The usual download is the program alone. It fetches every file from the
publisher when asked. In that case **the publisher distributes the file**,
under the publisher's licence, and the person who publishes this program takes
on no licence obligations for it.

An *offline package* has a `payload\` folder with the files already inside.
Whoever publishes that package **redistributes** those files, so:

* only open-source files may go in (`Payload::Allowed`). Manufacturers'
  installers never do (`Payload::Never`);
* GPL and LGPL binaries must come with their **complete corresponding source
  code**. `scripts/fetch-payload.ps1` puts the source archive of exactly the
  pinned release into `payload\sources\`, plus each licence text.

### Why RTL-SDR Blog V1.4.0 needs a source review

`rtlsdr.dll` from RTL-SDR Blog's release has libusb (LGPL-2.1) built into it.
The release doesn't say which libusb version or which build settings were
used. The source that goes with the binary must be complete, so before
redistributing this prebuilt DLL in an offline package, do one of these:

1. ask RTL-SDR Blog which libusb release and build settings were used, and put
   that libusb source in `payload\sources\` too; or
2. **(recommended)** build `rtlsdr.dll` from the `V1.4.0` tag and a pinned
   libusb release in this repository's CI, and ship exactly those sources. That
   also allows a build with no Visual C++ runtime dependency; or
3. leave it out of the offline package (the default). The program then fetches
   it from RTL-SDR Blog.

`scripts/fetch-payload.ps1` leaves it out unless `-IncludeNeedsSourceReview` is
given.

## Names and trademarks

RTL-SDR, HackRF, Airspy, SDRplay, RSP, bladeRF, ADALM-PLUTO, LimeSDR, USRP,
FUNcube, SDR#, HDSDR, SDR++, SDR Console, CubicSDR, SDRangel, SatDump, Gqrx,
SDRuno, SDRconnect, Zadig and the other names are their owners' names and
trademarks. They are used only to say what this program works with. This
project is not affiliated with, or endorsed by, any of them.

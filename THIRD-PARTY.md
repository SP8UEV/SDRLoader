# Third-party software

SDRLoader's own code is under the MIT licence (see `LICENSE`). It contains
**no third-party code**, and it **never bundles or redistributes** any driver:
it only *fetches, checks and runs* the packages below, each from its publisher's
own address, at the user's request. The copy the user receives comes straight
from the publisher, under the publisher's licence -- so publishing SDRLoader
carries no licence obligation for those files. The exact release, size and
SHA-256 (or the publisher's digital signature) are pinned in `src/catalog.cpp`.

## Linked library: libwdi (optional)

When built with `-DSDI_WITH_LIBWDI=ON`, the program links **libwdi**
(LGPL-2.1-or-later, https://github.com/pbatard/libwdi) to install WinUSB
automatically -- the same library Zadig is built on. This is a *linked
library*, not a fetched file, so the LGPL applies to the distributed binary:
anyone you give the program to must be able to replace libwdi with their own
build (ship libwdi's source, or your changes, and keep the program relinkable).
The default build does not include libwdi, and is MIT-only. The `SDRLoader-winusb`
release archive therefore ships the libwdi patches and the build script
(`libwdi-lgpl/`) so the binary stays relinkable.

libwdi installs a WinUSB driver package that it signs with a **self-signed
certificate it generates**; installing it places that certificate in the
machine's Trusted Root and Trusted Publisher stores. The program asks the user
to confirm this explicitly first (`driverinst::consentText`). WinUSB itself is
Microsoft's, and ships inside Windows.

## Packages the program can fetch or open

| Package | Publisher | Licence | How the program gets it |
|---|---|---|---|
| RTL-SDR Blog drivers V1.4.0 (`rtlsdr.dll`, x86 + x64) | RTL-SDR Blog | GPL-2.0-or-later (librtlsdr); LGPL-2.1 (libusb, built into `rtlsdr.dll`; `pthreadVC2.dll`); Microsoft redistributable (`msvcr100.dll`) | Download from GitHub release `V1.4.0`, SHA-256 checked |
| Zadig 2.9 | Akeo Consulting (Pete Batard) | GPL-3.0-or-later (Zadig), LGPL-3.0 (libwdi) | Download from GitHub release `v1.5.1`, signature "Akeo Consulting" checked |
| Microsoft Visual C++ Redistributable (x64, x86) | Microsoft | Microsoft Software License Terms (shown by the installer) | Download from `aka.ms`, signature "Microsoft Corporation" checked |
| bladeRF for Windows 2025.10 | Nuand | Nuand's terms (shown by the installer); libbladeRF LGPL-2.1 | Download from GitHub release `2025.10`, SHA-256 checked |
| PlutoSDR / M2k USB drivers v0.9 | Analog Devices | Analog Devices' terms (shown by the installer) | Download from GitHub release `v0.9`, signature "Analog Devices" checked |
| SDRplay API 3.x | SDRplay | Proprietary | Only the publisher's page is opened; the user downloads it there |
| FTDI D3XX driver (LimeSDR Mini) | FTDI | FTDI's driver licence | Only the publisher's page is opened; the user downloads it there |
| PothosSDR (SoapySDR and modules) | Pothosware / MyriadRF | Several open-source licences | Only the publisher's page is opened |

Every downloaded file is checked before use (size + SHA-256, or the publisher's
Authenticode signature). A file that fails is not used. Nothing about the user's
computer is sent anywhere; the program goes online only to fetch a file the user
asked for, from that file's publisher.

## Names and trademarks

RTL-SDR, HackRF, Airspy, SDRplay, RSP, bladeRF, ADALM-PLUTO, LimeSDR, USRP,
FUNcube, SDR#, HDSDR, SDR++, SDR Console, CubicSDR, SDRangel, SatDump, Gqrx,
SDRuno, SDRconnect, Zadig, FTDI and the other names are their owners' names and
trademarks. They are used only to say what this program works with. This
project is not affiliated with, or endorsed by, any of them.

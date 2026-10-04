# SDRLoader

An open-source helper for Windows 10 and 11. It finds the SDR radios plugged
into the computer and the SDR programs installed on it. Then it installs what
they are missing. You decide what happens, the files come from their
publishers and are checked before use, and every replaced file can be put back.

*(Polski opis niżej.)*

## What it does

1. **Radios.** It lists the SDR radios that are plugged in, and the USB driver
   Windows gave each one. Examples: an RTL-SDR still on the DVB-T TV driver, an
   RSP with no driver, a HackRF that some other installer put on libusbK.
2. **Programs.** It finds the SDR programs on the computer, and which
   `rtlsdr.dll` each of them has, if any.
3. **Plan.** It proposes what to do: install WinUSB for a radio (automatically
   where this build can, see below; otherwise Zadig), run a manufacturer's
   driver installer, put `rtlsdr.dll` where a program looks for it, or install
   the Visual C++ runtime that DLL needs.
4. **You tick, then press *Install ticked*.** Every file is checked first. Its
   size and SHA-256 must match what the publisher published, or it must carry
   the publisher's digital signature. A file that fails is not used. A file
   that is replaced is backed up first, and *Undo last installation* puts it
   back.

## What it never does

* Nothing is changed until you press *Install ticked*. Items that would
  replace an existing file start unticked.
* Nothing about your computer is sent anywhere. The program goes online only
  to fetch a file you asked for, from that file's publisher.
* It never touches programs that ship their own drivers (SDR++, SDR Console,
  CubicSDR, SDRangel, SatDump, Gqrx). It only reports them.
* It never redistributes manufacturers' software. Their installers are
  downloaded from them and run in front of you, with their own licence terms.
  The SDRplay API is only linked: you download it from SDRplay.
* It never searches whole drives unless you ask. The standard scan looks only
  where programs normally are: the installed-programs list, Program Files, the
  Start menu and desktop shortcuts, Desktop, Downloads, Documents and folders
  at the root of a drive. *Full scan of all drives* goes further, and only
  when you press it. Both scans look at file names only.

## Radios

### Automatic WinUSB install (and the certificate)

Giving an RTL-SDR the WinUSB driver is the step beginners stumble on. This
program can do it **without the Zadig window**, using **libwdi** (LGPL-2.1,
the library Zadig itself is built on). For the one radio it builds a small
driver package (an INF), signs it with a certificate it generates, adds that
certificate to this computer's trusted-certificate stores so Windows accepts
the driver, and asks Windows to bind WinUSB. This is exactly what Zadig does
when you press *Install Driver*.

Because it adds a driver certificate and changes a device driver, it needs
administrator rights **and** a clear yes: before anything happens, a prompt
spells out each of these steps, and nothing runs until you agree. It changes
only that one radio's driver, which Device Manager can undo, and the
certificate can be removed in `certmgr.msc`.

This path is compiled in only when the build includes libwdi (see *Building*).
Without it, the program proposes Zadig instead and the step is manual.

| Radio | What it needs on Windows |
|---|---|
| RTL-SDR (RTL2832U, incl. RTL-SDR Blog V3/V4) | WinUSB on interface 0 (installed automatically, or with Zadig), plus `rtlsdr.dll` for the programs that load it |
| HackRF One, Airspy R2/Mini, Airspy HF+ | WinUSB, which Windows 8 and later assign by themselves |
| SDRplay RSP | The SDRplay API (it contains the USB driver) |
| Nuand bladeRF | Nuand's installer (driver, libbladeRF, FPGA images) |
| ADALM-PLUTO | Analog Devices' USB drivers |
| LimeSDR-USB | WinUSB (installed automatically, or with Zadig) |
| Ettus USRP B200/B210 | WinUSB (installed automatically, or with Zadig) |
| LimeSDR Mini / Mini 2 | FTDI's D3XX driver for the FT601 bridge (WinUSB can't drive it); its download page is opened |
| FUNcube Dongle Pro/Pro+ | Nothing (Windows' audio and HID drivers) |

To actually *use* a LimeSDR or USRP you still install its host software (LimeSuite
or UHD); SDRLoader only sets up the USB driver.

## Programs

| Program | rtlsdr.dll |
|---|---|
| ChameleonSDR | 64-bit, in `%APPDATA%\C-SDR\PUT_SDR_FILES_HERE` |
| SDR# | Beside `SDRSharp*.exe`, in the build the program uses (x86 or x64) |
| HDSDR | Beside `HDSDR.exe`, when an RTL ExtIO plug-in is there |
| SDR++, SDR Console, CubicSDR, SDRangel, SatDump, Gqrx | Ship their own: reported, never changed |
| SDRuno, SDRconnect | Use the SDRplay API |

A radio or a program is added in one place, `src/catalog.cpp`.

## Download

* **`SDRLoader.zip`**: the program. It fetches each file from its
  publisher when needed.
* **`SDRLoader-offline.zip`**: the same, plus a `payload\` folder
  with the open-source files already inside, each with its source code. See
  `THIRD-PARTY.md` for what may go in and why.

Two programs are included. `sdrloader.exe` is the window.
`sdrloader-cli.exe --scan` prints the same scan as text, which is
useful for support requests.

## Building

You need CMake 3.16 or later and one of these compilers: MinGW-w64 GCC (MSYS2
UCRT64 or WinLibs) or Visual Studio 2022.

```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
build\sdi-tests.exe
```

**Automatic WinUSB install (libwdi).** The default build does not include it,
so the driver step is Zadig. To compile the automatic path in, build libwdi
and point CMake at the result. `scripts/build-libwdi.sh` does the libwdi build
(it clones libwdi `v1.5.1`, applies the patches in `third_party/libwdi-patches/`
for a WDK-free inbox-WinUSB build, and produces `libwdi.a`):

```
scripts/build-libwdi.sh --clone external/libwdi v1.5.1
cmake -S . -B build-wdi -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DSDI_WITH_LIBWDI=ON \
      -DLIBWDI_INCLUDE_DIR=external/libwdi/libwdi \
      -DLIBWDI_LIBRARY=external/libwdi/libwdi/libwdi.a
cmake --build build-wdi
```

(The script prints the exact `-D…` line to use. Run it from Git Bash / MSYS2
with the same MinGW-w64 GCC used for the project.)

libwdi is LGPL-2.1-or-later. Linking it in makes that part of the program
LGPL: ship libwdi's source and our patches (both reproduced by the script) and
keep the program relinkable, as the LGPL asks. See `THIRD-PARTY.md` and
`third_party/libwdi-patches/`.

The programs are linked statically, so they need nothing installed to run.
`scripts/fetch-payload.ps1` builds the offline package's `payload\` folder.
`.github/workflows/build.yml` builds, tests and packages both zips.

## Licence

The code in this repository is under the MIT licence (`LICENSE`). The
packages it can install are other people's software under their own
licences: see `THIRD-PARTY.md`. The names of radios, programs and companies
are their owners' trademarks, used only to say what this works with. This
project is not affiliated with any of them.


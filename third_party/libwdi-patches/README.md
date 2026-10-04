# libwdi patches (automatic WinUSB install)

The automatic WinUSB install uses **libwdi** (LGPL-2.1-or-later,
<https://github.com/pbatard/libwdi>), the library Zadig is built on. libwdi has
no CMake build, so `scripts/build-libwdi.sh` builds a static `libwdi.a` with the
project's MinGW-w64 GCC, for **WinUSB on 64-bit Windows 10/11, without the
Windows Driver Kit**.

Three small changes are applied to libwdi `v1.5.1`. They are recorded here as
diffs, and the build script applies the same changes programmatically (so it
works regardless of line-ending quirks). This directory, together with the
script, is the record of our modifications to the LGPL library; the program
links `libwdi.a` statically and can be relinked against a different libwdi
build by re-running the script and pointing CMake at the result.

| File | Patch | Why |
|---|---|---|
| `libwdi/winusb.inf.in` | `winusb.inf.in.patch` | Remove the co-installer / `SourceDisks` sections. On Windows 10/11 WinUSB is an **inbox** driver and needs no co-installer, so the WDK redistributables (`WinUSBCoInstaller2.dll`, `WdfCoInstaller*.dll`) are not required and not shipped. |
| `libwdi/winusb.cat.in` | `winusb.cat.in.patch` | With no co-installer DLLs, the self-signed catalogue covers only the `.inf`. |
| `libwdi/libwdi.c` | `libwdi.c.patch` | `wdi_is_driver_supported(WDI_WINUSB)` returns `TRUE`. Upstream returns `FALSE` unless built against the WDK (`#if defined(WDK_DIR)`); without the fix libwdi silently falls back to the USB-serial (CDC) driver. WinUSB is inbox on every OS this targets (Windows 7+), so `TRUE` is correct. |

`config.mingw.h` is the `config.h` the script generates (WinUSB only, 64-bit,
no WDK; `USER_DIR` points at an empty folder purely to satisfy libwdi's
`embedder.h` "at least one driver directory" check -- no user files are
embedded).

## Build

```
scripts/build-libwdi.sh --clone external/libwdi v1.5.1
```

Then configure the project with the `-DSDI_WITH_LIBWDI=ON -DLIBWDI_INCLUDE_DIR=...
-DLIBWDI_LIBRARY=...` line the script prints.

## What the result produces

For a target device, `wdi_prepare_driver` generates a standard inbox-WinUSB
device INF (`Class=USBDevice`, `Include=winusb.inf`, `Needs=WINUSB.NT`,
`AddService=WinUSB`), with the device's interface GUID and no co-installer
references. `DriverVer` is dated today with version `0.0.0.0` (there is no WDK
co-installer to read a version from), which is fine for a self-signed inbox
package. `wdi_install_driver` then signs it with a generated certificate,
installs that certificate to the machine's trusted stores, and binds WinUSB --
exactly as Zadig does.

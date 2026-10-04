/* Minimal config.h for building libwdi with MinGW (WinUSB only, 64-bit).
 * No WDK: the WinUSB inbox driver on Windows 10/11 needs no co-installer, so
 * the co-installer sections are removed from winusb.inf.in (see the patch in
 * this repo). USER_DIR points at an empty folder only to satisfy embedder.h's
 * "at least one driver directory" check; no user files are embedded. */
#pragma once
#define _GNU_SOURCE
#define WDF_VER 1011
#define OPT_M64
#define ENABLE_LOGGING 1
#define INCLUDE_DEBUG_LOGGING 1
#define USER_DIR "C:/Users/Maciek/Documents/Cloude/SDRLoader/external/libwdi/mingw/userdir"

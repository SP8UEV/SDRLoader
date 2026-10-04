cmake_minimum_required(VERSION 3.16)
project(SDRLoader VERSION 0.1.0 LANGUAGES CXX RC)

if(NOT WIN32)
  message(FATAL_ERROR "SDRLoader is a Windows program.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

# Windows 10 and later.
add_compile_definitions(UNICODE _UNICODE NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

if(MSVC)
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8;/W4;/permissive-;/EHsc>")
  # The C runtime is linked in, so the programs need nothing installed to run.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wno-missing-field-initializers>")
  # wmain/wWinMain, and no MinGW DLLs to carry around.
  add_link_options(-municode -static -static-libgcc -static-libstdc++ "$<$<CONFIG:Release>:-s>")

  # GCC links a default manifest (default-manifest.o) into every program, and
  # these programs bring their own (res/app.manifest: common controls 6, DPI,
  # asInvoker). Two manifests with the same ID and language are something ld
  # refuses to merge, so an empty default-manifest.o, found first through -B,
  # takes the toolchain's place.
  set(NO_DEFAULT_MANIFEST "${CMAKE_BINARY_DIR}/no-default-manifest")
  file(MAKE_DIRECTORY "${NO_DEFAULT_MANIFEST}")
  add_custom_command(OUTPUT "${NO_DEFAULT_MANIFEST}/default-manifest.o"
    COMMAND "${CMAKE_CXX_COMPILER}" -c "${CMAKE_CURRENT_SOURCE_DIR}/res/empty.cpp"
            -o "${NO_DEFAULT_MANIFEST}/default-manifest.o"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/res/empty.cpp"
    VERBATIM)
  add_custom_target(no_default_manifest DEPENDS "${NO_DEFAULT_MANIFEST}/default-manifest.o")
  add_link_options("-B${NO_DEFAULT_MANIFEST}/")
endif()

add_library(sdi_core STATIC
  src/util.cpp
  src/i18n.cpp
  src/net.cpp
  src/catalog.cpp
  src/usbscan.cpp
  src/progscan.cpp
  src/planner.cpp
  src/actions.cpp
  src/driverinstall.cpp)
target_include_directories(sdi_core PUBLIC src)
target_link_libraries(sdi_core PUBLIC
  setupapi cfgmgr32 winhttp bcrypt wintrust crypt32 shell32 ole32 uuid advapi32 user32)

# Automatic WinUSB install, via libwdi (LGPL-2.1-or-later), the library Zadig is
# built on. libwdi has no CMake build of its own, so build it once (its own way)
# and point these at the result:
#   cmake -DSDI_WITH_LIBWDI=ON -DLIBWDI_INCLUDE_DIR=<dir with libwdi.h> \
#         -DLIBWDI_LIBRARY=<path to libwdi.a or libwdi.lib> ...
# Without it the program still builds; it then falls back to launching Zadig.
option(SDI_WITH_LIBWDI "Build with libwdi for automatic WinUSB installation" OFF)
if(SDI_WITH_LIBWDI)
  if(NOT LIBWDI_INCLUDE_DIR OR NOT LIBWDI_LIBRARY)
    message(FATAL_ERROR "SDI_WITH_LIBWDI is ON but LIBWDI_INCLUDE_DIR / LIBWDI_LIBRARY are not set.")
  endif()
  # Resolve relative paths against the repo root so a bare "external/..." is a
  # file path, not a -l library name, and so it works whatever the build dir is.
  get_filename_component(LIBWDI_LIBRARY "${LIBWDI_LIBRARY}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  get_filename_component(LIBWDI_INCLUDE_DIR "${LIBWDI_INCLUDE_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT EXISTS "${LIBWDI_LIBRARY}")
    message(FATAL_ERROR "LIBWDI_LIBRARY not found: ${LIBWDI_LIBRARY} -- run scripts/build-libwdi.sh first.")
  endif()
  target_compile_definitions(sdi_core PUBLIC SDI_HAVE_LIBWDI)
  target_include_directories(sdi_core PUBLIC "${LIBWDI_INCLUDE_DIR}")
  # libwdi.a must precede the system import libraries it calls into (MinGW ld
  # resolves left to right), so the ones it needs are re-listed after it --
  # newdev (driver install), ntdll (RtlGetVersion), version (driver version
  # info), setupapi/cfgmgr32 (device enumeration), ole32/crypt32/advapi32
  # (the self-signed certificate).
  target_link_libraries(sdi_core PUBLIC
    "${LIBWDI_LIBRARY}" newdev ntdll version setupapi cfgmgr32 ole32 crypt32 advapi32 shell32 user32 uuid)
  message(STATUS "libwdi: ${LIBWDI_LIBRARY} (automatic WinUSB install enabled)")
else()
  message(STATUS "libwdi: not used (automatic WinUSB install disabled; Zadig is the fallback)")
endif()

# The window: what users run.
add_executable(sdrloader WIN32 src/gui.cpp src/gui_main.cpp res/app.rc)
target_link_libraries(sdrloader PRIVATE sdi_core comctl32 gdi32)
target_include_directories(sdrloader PRIVATE res)

# The same scan as text, for support and for scripts/fetch-payload.ps1.
add_executable(sdrloader-cli src/cli_main.cpp res/cli.rc)
target_link_libraries(sdrloader-cli PRIVATE sdi_core)
target_include_directories(sdrloader-cli PRIVATE res)

add_executable(sdi-tests tests/test_core.cpp res/cli.rc)
target_link_libraries(sdi-tests PRIVATE sdi_core)
target_include_directories(sdi-tests PRIVATE res)

# A manual check of the network path; not run by the tests.
add_executable(sdi-net-probe EXCLUDE_FROM_ALL tests/net_probe.cpp res/cli.rc)
target_link_libraries(sdi-net-probe PRIVATE sdi_core)
target_include_directories(sdi-net-probe PRIVATE res)

if(TARGET no_default_manifest)
  foreach(t sdrloader sdrloader-cli sdi-tests sdi-net-probe)
    add_dependencies(${t} no_default_manifest)
  endforeach()
endif()

enable_testing()
add_test(NAME core COMMAND sdi-tests)

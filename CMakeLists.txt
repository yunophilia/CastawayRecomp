cmake_minimum_required(VERSION 3.21)

# Build with the presets in CMakePresets.json: clang-cl + Ninja on Windows (MSVC also
# works), clang + Ninja on Linux. The runtime is Windows-only (D3D11, XAudio2, XInput),
# so other platforms only build the recompiled game code for now.
project(CastawayRecomp LANGUAGES CXX)

# Windows builds go to out/, others to out/<system> (their own shader cache, config and saves)
if(WIN32)
    set(CASTAWAY_OUT ${CMAKE_SOURCE_DIR}/out)
else()
    string(TOLOWER ${CMAKE_SYSTEM_NAME} _system)
    set(CASTAWAY_OUT ${CMAKE_SOURCE_DIR}/out/${_system})
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

# BKR_CALL_COUNTS: instrumented build counting calls to every game function, for
# frame-rate analysis with TAS scripts (docs/tas.md); the exe is CastawayRecomp_counts.
option(BKR_CALL_COUNTS "Count calls to every recompiled function" OFF)
if(BKR_CALL_COUNTS)
    add_compile_definitions(BKR_CALL_COUNTS)
endif()

# MSVC_FRONTEND: cl or clang-cl (MSVC-style flags); CLANG: clang-cl or clang
set(MSVC_FRONTEND ${MSVC})
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CLANG ON)
endif()

# Visual Studio 2026 is recommended; 2022 (MSVC 19.30) is the oldest supported. clang-cl
# reports the version of the Visual Studio libraries it found.
if(MSVC_FRONTEND)
    if(CLANG)
        set(VS_LIB_VERSION ${CMAKE_CXX_SIMULATE_VERSION})
    else()
        set(VS_LIB_VERSION ${CMAKE_CXX_COMPILER_VERSION})
    endif()
    if(VS_LIB_VERSION VERSION_LESS 19.30)
        message(FATAL_ERROR "Visual Studio 2022 or newer is required (found MSVC ${VS_LIB_VERSION}); "
                            "install Visual Studio 2026 (Build Tools are enough) with the C++ workload")
    endif()
endif()

# ---------------------------------------------------------------- disc image installer
# nod (https://github.com/encounter/nod, MIT OR Apache-2.0, used under MIT) reads the
# user's disc image; the prebuilt package is downloaded at configure time.
include(FetchContent)
set(NOD_VERSION v2.0.0-alpha.12)
if(WIN32)
    FetchContent_Declare(nod_prebuilt
        URL https://github.com/encounter/nod/releases/download/${NOD_VERSION}/libnod-windows-x86_64.tar.gz
        URL_HASH SHA256=c9dfecf8e52066a5222f3cb1cad18d0740c958ce65bf60cb84e780a3856ed881
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
else()
    FetchContent_Declare(nod_prebuilt
        URL https://github.com/encounter/nod/releases/download/${NOD_VERSION}/libnod-linux-x86_64.tar.gz
        URL_HASH SHA256=920adb1d2386c8728e0521f8d52809057c04bd46913433a0000ee0931d08d69f
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
endif()
FetchContent_MakeAvailable(nod_prebuilt)
block()
    find_package(nod REQUIRED CONFIG PATHS ${nod_prebuilt_SOURCE_DIR} NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)

    # with the host helpers and SHA-1, which the runtime gets through this library too
    add_library(bkr_installer STATIC runtime/install/install.cpp runtime/install/verify.cpp runtime/install/releases.cpp
                                     runtime/kernel/host.cpp runtime/kernel/sha1.cpp)
    target_include_directories(bkr_installer PUBLIC runtime)
    if(WIN32)
        target_compile_definitions(bkr_installer PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
        target_link_libraries(bkr_installer PUBLIC winmm)   # kernel/host.cpp: timeBeginPeriod
    else()
        target_compile_options(bkr_installer PRIVATE -include ${CMAKE_SOURCE_DIR}/runtime/compat.h)
    endif()
    target_link_libraries(bkr_installer PUBLIC nod::nod_static)
    if(MSVC_FRONTEND)
        target_compile_options(bkr_installer PRIVATE /EHsc /utf-8)
        # nod's C dependencies are built against the DLL runtime; we link the static one
        target_link_options(bkr_installer INTERFACE /NODEFAULTLIB:msvcrt /IGNORE:4217)
    endif()

    # bkr_install <image>: installs game/extracted/DATA before the first build_generated.py
    add_executable(bkr_install tools/bkr_install/main.cpp)
    target_link_libraries(bkr_install PRIVATE bkr_installer)
    if(MSVC_FRONTEND)
        target_compile_options(bkr_install PRIVATE /EHsc /utf-8)
    endif()
    set_target_properties(bkr_install PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CASTAWAY_OUT}
                                                 RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CASTAWAY_OUT}
                                                 RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CASTAWAY_OUT})
endblock()

# ---------------------------------------------------------------- Dear ImGui (launcher, settings overlay)
# https://github.com/ocornut/imgui (MIT), downloaded at configure time
block()
    FetchContent_Declare(imgui
        URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.9b.tar.gz
        URL_HASH SHA256=21d8a0a565e85dce943e375db00812c2f3f0ab21f3f0f7964e364a63422d7f99
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(imgui)
    add_library(imgui STATIC ${imgui_SOURCE_DIR}/imgui.cpp ${imgui_SOURCE_DIR}/imgui_draw.cpp
                             ${imgui_SOURCE_DIR}/imgui_tables.cpp ${imgui_SOURCE_DIR}/imgui_widgets.cpp
                             ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
    target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
    target_compile_definitions(imgui PUBLIC IMGUI_DISABLE_OBSOLETE_FUNCTIONS)
endblock()

# ---------------------------------------------------------------- SDL3 (gamepads of any brand)
# https://github.com/libsdl-org/SDL (zlib), prebuilt Visual C++ package downloaded at configure time
if(WIN32)
    FetchContent_Declare(sdl3_prebuilt
        URL https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-devel-3.4.16-VC.zip
        URL_HASH SHA256=1a784cb2a5c64d56fe7a62090fe9d242d9865f235e4ea9678f1a6ba4e693e7de
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(sdl3_prebuilt)
    find_package(SDL3 REQUIRED CONFIG PATHS ${sdl3_prebuilt_SOURCE_DIR}/cmake NO_DEFAULT_PATH NO_CMAKE_FIND_ROOT_PATH)
else()
    find_package(SDL3 REQUIRED CONFIG)   # the distribution's (libsdl3-dev)
endif()
target_link_libraries(imgui PUBLIC SDL3::SDL3)   # the ImGui SDL3 backend

# ---------------------------------------------------------------- Linux: D3D11 on Vulkan
# DXVK's native build (https://github.com/doitsujin/dxvk, zlib) implements D3D11/DXGI on
# Vulkan and presents to SDL windows; vkd3d-utils (https://gitlab.winehq.org/wine/vkd3d,
# LGPL-2.1+) provides D3DCompile for the generated HLSL. Both are built into
# CASTAWAY_DEPS (see docs/linux.md).
if(NOT WIN32)
    set(CASTAWAY_DEPS "$ENV{HOME}/castaway-deps/prefix" CACHE PATH "prefix with DXVK-native and vkd3d")
    find_library(DXVK_D3D11 dxvk_d3d11 PATHS ${CASTAWAY_DEPS}/lib/x86_64-linux-gnu ${CASTAWAY_DEPS}/lib REQUIRED NO_DEFAULT_PATH)
    find_library(DXVK_DXGI dxvk_dxgi PATHS ${CASTAWAY_DEPS}/lib/x86_64-linux-gnu ${CASTAWAY_DEPS}/lib REQUIRED NO_DEFAULT_PATH)
    find_library(VKD3D_UTILS vkd3d-utils PATHS ${CASTAWAY_DEPS}/lib REQUIRED NO_DEFAULT_PATH)
    add_library(d3d11_native INTERFACE)
    target_include_directories(d3d11_native INTERFACE ${CASTAWAY_DEPS}/include/dxvk)
    # vkd3d's compiler uses the ms_abi convention: callers go through runtime/gx/d3dcompiler_posix.cpp
    target_compile_definitions(d3d11_native INTERFACE D3DCompile=Host_D3DCompile D3DCreateBlob=Host_D3DCreateBlob)
    target_link_libraries(d3d11_native INTERFACE ${DXVK_D3D11} ${DXVK_DXGI} ${VKD3D_UTILS})
    target_link_libraries(imgui PUBLIC d3d11_native)
endif()

if(NOT EXISTS ${CMAKE_SOURCE_DIR}/generated/sources.cmake)
    # Reconfigure by itself once tools/build_generated.py has filled generated/
    file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/generated)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/generated)
    message(WARNING "generated/ is empty: only bkr_install is built. Install the game data "
                    "(out/bkr_install <disc image>), run tools/build_generated.py, then build again.")
    return()
endif()
include(${CMAKE_SOURCE_DIR}/generated/sources.cmake)

# ---------------------------------------------------------------- recompiled game code
add_library(ppcrecomp OBJECT ${PPC_RECOMP_SOURCES})
target_include_directories(ppcrecomp PUBLIC runtime/cpu generated)
if(MSVC_FRONTEND)
    # always optimized, also in Debug: unoptimized recompiled code is far too slow
    target_compile_options(ppcrecomp PRIVATE /O2 /Ob2 /GS- /EHs-c- /std:c++latest)
    if(CLANG)
        target_compile_options(ppcrecomp PRIVATE -Wno-unused-label -Wno-unused-variable)
    else()
        target_compile_options(ppcrecomp PRIVATE /bigobj /wd4102 /wd4244 /wd4146 /wd4018)
    endif()
else()
    target_compile_options(ppcrecomp PRIVATE -O2 -fno-exceptions -fno-strict-aliasing -w)
endif()

# ---------------------------------------------------------------- runtime
file(GLOB_RECURSE RUNTIME_SOURCES CONFIGURE_DEPENDS runtime/*.cpp)
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "/runtime/install/")
list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "/runtime/kernel/(host|sha1)\.cpp$")   # in bkr_installer
if(WIN32)
    list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "_posix\\.cpp$")
else()
    list(FILTER RUNTIME_SOURCES EXCLUDE REGEX "_win32\\.cpp$")
endif()
add_executable(CastawayRecomp ${RUNTIME_SOURCES})
target_include_directories(CastawayRecomp PRIVATE runtime runtime/cpu generated)
target_sources(CastawayRecomp PRIVATE $<TARGET_OBJECTS:ppcrecomp>)
# version shown in the launcher and the overlay, refreshed on every build
set(BKR_VERSION_CPP ${CMAKE_BINARY_DIR}/bkr_version.cpp)
add_custom_target(bkr_version
    COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${CMAKE_SOURCE_DIR} -DVERSION_OUT=${BKR_VERSION_CPP}
            -P ${CMAKE_SOURCE_DIR}/tools/version.cmake
    BYPRODUCTS ${BKR_VERSION_CPP} VERBATIM)
target_sources(CastawayRecomp PRIVATE ${BKR_VERSION_CPP})
add_dependencies(CastawayRecomp bkr_version)
if(WIN32)
    target_link_libraries(CastawayRecomp PRIVATE bkr_installer imgui SDL3::SDL3 d3d11 dxgi d3dcompiler winmm ole32 uuid shell32 dbghelp)
    # SDL3.dll next to the exe
    add_custom_command(TARGET CastawayRecomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3> $<TARGET_FILE_DIR:CastawayRecomp>)
else()
    target_link_libraries(CastawayRecomp PRIVATE bkr_installer imgui SDL3::SDL3 d3d11_native pthread dl)
    # find DXVK and vkd3d next to the binary (lib/) or in the build prefix
    set_target_properties(CastawayRecomp PROPERTIES BUILD_RPATH "$ORIGIN/lib;${CASTAWAY_DEPS}/lib;${CASTAWAY_DEPS}/lib/x86_64-linux-gnu"
                                                    INSTALL_RPATH "$ORIGIN/lib")
endif()
# the scripts in tools/ and the shader cache expect the game in out/
set_target_properties(CastawayRecomp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CASTAWAY_OUT}
                                                   RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO ${CASTAWAY_OUT}
                                                   RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CASTAWAY_OUT})
if(BKR_CALL_COUNTS)
    set_target_properties(CastawayRecomp PROPERTIES OUTPUT_NAME CastawayRecomp_counts)
endif()
if(WIN32)
    target_compile_definitions(CastawayRecomp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(CastawayRecomp PRIVATE -Wno-unused-function -Wno-unused-variable -fno-strict-aliasing
                                                  -include ${CMAKE_SOURCE_DIR}/runtime/compat.h)
endif()
if(MSVC_FRONTEND)
    target_compile_options(CastawayRecomp PRIVATE /EHsc /std:c++latest /utf-8 $<$<CONFIG:RelWithDebInfo>:/Ob2>)
    if(CLANG)
        target_compile_options(CastawayRecomp PRIVATE -Wno-unused-function -Wno-unused-variable)
    else()
        target_compile_options(CastawayRecomp PRIVATE /wd4611)
    endif()
    target_link_options(CastawayRecomp PRIVATE /STACK:268435456 /SUBSYSTEM:CONSOLE)
endif()

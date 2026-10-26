@echo off
rem Incremental build with the windows-clang preset (configures on first use).
rem Sets up the newest Visual Studio (2022 or later) environment, preferring a
rem standalone LLVM install when there is one. The exe goes to out\CastawayRecomp.exe;
rem "dev_build counts" builds the call-counting variant (out\CastawayRecomp_counts.exe).
setlocal
cd /d %~dp0..
set "PRESET=windows-clang"
if /i "%~1"=="counts" set "PRESET=windows-clang-counts"
if not defined VSCMD_VER (
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -version [17.0^,^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
    if not defined VSDIR (
        echo Visual Studio 2022 or newer with the C++ workload is required
        exit /b 1
    )
    set "PATH=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer;%PATH%"
    call "%%VSDIR%%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 -no_logo
)
rem Visual Studio's CMake and Ninja, ahead of any older standalone ones
set "PATH=%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSINSTALLDIR%Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja;%PATH%"
if exist "%ProgramFiles%\LLVM\bin\clang-cl.exe" set "PATH=%ProgramFiles%\LLVM\bin;%PATH%"
if not exist build\%PRESET%\CMakeCache.txt (
    cmake --preset %PRESET% > out\configure.log 2>&1 || (type out\configure.log & echo CONFIGURE FAILED & exit /b 1)
)
cmake --build --preset %PRESET% > out\build.log 2>&1
if errorlevel 1 (
    findstr /C:"error" /C:"FAILED" out\build.log
    echo BUILD FAILED
    exit /b 1
)
echo BUILD OK

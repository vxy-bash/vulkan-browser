@echo off
setlocal EnableDelayedExpansion
title VulkanBrowser — Build ^& Run

:: ============================================================
:: build_and_run.bat
:: Kompiliert VulkanBrowser und startet ihn danach direkt.
::
:: Voraussetzungen (werden automatisch geprueft):
::   - CMake   >= 3.25        https://cmake.org/download/
::   - Vulkan SDK             https://vulkan.lunarg.com/
::   - Qt6 (MSVC oder MinGW)  https://www.qt.io/download-open-source
::   - Visual Studio 2022  ODER  LLVM/Clang  ODER  MinGW-w64
::   - Skia (vorgebaut mit Vulkan-Backend)
::
:: Verwendung:
::   build_and_run.bat                         Release-Build + Start
::   build_and_run.bat --debug                 Debug-Build + Start
::   build_and_run.bat --clean                 Build-Ordner loeschen
::   build_and_run.bat --skia-dir C:\skia      Skia-Pfad manuell angeben
::   build_and_run.bat --no-run                Nur bauen, nicht starten
::   build_and_run.bat --jobs 8                Parallel-Jobs
:: ============================================================

:: ---------- Farben (ANSI, funktioniert ab Windows 10 1511) ----------
set "ESC="
for /f %%a in ('echo prompt $E ^| cmd') do set "ESC=%%a"
set "RED=%ESC%[91m"
set "GRN=%ESC%[92m"
set "YLW=%ESC%[93m"
set "CYN=%ESC%[96m"
set "BLD=%ESC%[1m"
set "RST=%ESC%[0m"

:: ---------- Standard-Werte ----------
set "BUILD_TYPE=Release"
set "CLEAN=0"
set "NO_RUN=0"
set "JOBS=%NUMBER_OF_PROCESSORS%"
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "BUILD_DIR=%SCRIPT_DIR%\build"
set "DIST_DIR=%SCRIPT_DIR%\dist"
set "SKIA_DIR_ARG="
set "BINARY=%BUILD_DIR%\Release\vulkan-browser.exe"

:: ---------- Argumente parsen ----------
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--debug"    ( set "BUILD_TYPE=Debug"  & set "BINARY=%BUILD_DIR%\Debug\vulkan-browser.exe" & shift & goto parse_args )
if /i "%~1"=="--clean"    ( set "CLEAN=1"            & shift & goto parse_args )
if /i "%~1"=="--no-run"   ( set "NO_RUN=1"           & shift & goto parse_args )
if /i "%~1"=="--jobs"     ( set "JOBS=%~2"           & shift & shift & goto parse_args )
if /i "%~1"=="--skia-dir" ( set "SKIA_DIR_ARG=%~2"  & shift & shift & goto parse_args )
if /i "%~1"=="--help"     goto show_help
echo %RED%[FEHLER]%RST% Unbekanntes Argument: %~1  (--help fuer Hilfe)
exit /b 1

:show_help
echo.
echo  build_and_run.bat [Optionen]
echo.
echo  --debug            Debug-Build mit Validierungslayern
echo  --clean            Build-Ordner loeschen und neu bauen
echo  --no-run           Nur bauen, Browser nicht starten
echo  --skia-dir ^<Pfad^>  Pfad zum vorgebauten Skia-Verzeichnis
echo  --jobs ^<N^>         Anzahl Parallel-Jobs (Standard: CPU-Kerne)
echo  --help             Diese Hilfe anzeigen
echo.
exit /b 0

:args_done

echo.
echo %BLD%%CYN%============================================================%RST%
echo %BLD%%CYN%   VulkanBrowser — Build ^& Run   [%BUILD_TYPE%]%RST%
echo %BLD%%CYN%============================================================%RST%
echo.

:: ============================================================
:: 1. VORAUSSETZUNGEN PRUEFEN
:: ============================================================
echo %CYN%[1/5]%RST% Pruefe Voraussetzungen...

:: --- CMake ---
where cmake >nul 2>&1
if errorlevel 1 (
    echo %RED%[FEHLER]%RST% CMake nicht gefunden.
    echo        Download: https://cmake.org/download/
    echo        Oder:     winget install Kitware.CMake
    exit /b 1
)
for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "version"') do set "CMAKE_VER=%%v"
echo %GRN%[  OK  ]%RST% CMake %CMAKE_VER%

:: --- Vulkan SDK ---
if defined VULKAN_SDK (
    echo %GRN%[  OK  ]%RST% Vulkan SDK: %VULKAN_SDK%
) else (
    where vkconfig >nul 2>&1
    if errorlevel 1 (
        echo %YLW%[ WARN ]%RST% VULKAN_SDK nicht gesetzt — CMake sucht selbst.
        echo        Falls der Build fehlschlaegt: https://vulkan.lunarg.com/sdk/home
    ) else (
        echo %GRN%[  OK  ]%RST% Vulkan SDK im PATH gefunden
    )
)

:: --- Compiler: MSVC / Clang / MinGW ---
set "GENERATOR="
set "COMPILER_INFO="

:: Visual Studio 2022 pruefen
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (
        `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`
    ) do set "VS_PATH=%%i"
)

if defined VS_PATH (
    set "GENERATOR=Visual Studio 17 2022"
    set "COMPILER_INFO=MSVC (Visual Studio 2022)"
    echo %GRN%[  OK  ]%RST% Compiler: Visual Studio 2022
    goto compiler_found
)

:: Clang pruefen
where clang++ >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=3" %%v in ('clang++ --version 2^>^&1 ^| findstr /i "version"') do set "CLANG_VER=%%v"
    set "GENERATOR=Ninja"
    set "COMPILER_INFO=Clang++ !CLANG_VER!"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang"
    echo %GRN%[  OK  ]%RST% Compiler: Clang++ !CLANG_VER!
    goto compiler_found
)

:: MinGW pruefen
where g++ >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=3" %%v in ('g++ --version 2^>^&1 ^| findstr /i "g++"') do set "GCC_VER=%%v"
    set "GENERATOR=MinGW Makefiles"
    set "COMPILER_INFO=MinGW g++ !GCC_VER!"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc"
    echo %GRN%[  OK  ]%RST% Compiler: MinGW g++
    goto compiler_found
)

echo %RED%[FEHLER]%RST% Kein C++20-Compiler gefunden.
echo        Optionen:
echo          1. Visual Studio 2022: https://visualstudio.microsoft.com/
echo          2. LLVM/Clang:         winget install LLVM.LLVM
echo          3. MinGW-w64:          winget install GnuWin32.Make
exit /b 1

:compiler_found

:: --- Ninja (fuer Clang/MinGW besser als make) ---
if "%GENERATOR%"=="Ninja" (
    where ninja >nul 2>&1
    if errorlevel 1 (
        echo %YLW%[ WARN ]%RST% Ninja nicht gefunden — verwende NMake.
        set "GENERATOR=NMake Makefiles"
    ) else (
        echo %GRN%[  OK  ]%RST% Ninja gefunden
    )
)

:: --- Qt6 ---
if defined Qt6_DIR (
    echo %GRN%[  OK  ]%RST% Qt6_DIR: %Qt6_DIR%
) else if defined QTDIR (
    echo %GRN%[  OK  ]%RST% QTDIR: %QTDIR%
    set "Qt6_DIR=%QTDIR%\lib\cmake\Qt6"
) else (
    echo %YLW%[ WARN ]%RST% Qt6_DIR nicht gesetzt.
    echo        Setze Qt6_DIR, z.B.:
    echo          set Qt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
    echo        Download: https://www.qt.io/download-open-source
)

:: --- Skia ---
if defined SKIA_DIR_ARG (
    set "SKIA_DIR=%SKIA_DIR_ARG%"
)
if not defined SKIA_DIR (
    set "SKIA_DIR=%SCRIPT_DIR%\third_party\skia"
    echo %YLW%[ WARN ]%RST% SKIA_DIR nicht gesetzt — verwende %SKIA_DIR%
    echo        Alternativ: build_and_run.bat --skia-dir C:\skia
)

if not exist "%SKIA_DIR%\out\Release\skia.lib" (
    echo %RED%[FEHLER]%RST% Skia-Library nicht gefunden: %SKIA_DIR%\out\Release\skia.lib
    echo.
    echo        Skia muss vorher gebaut werden:
    echo          git clone https://skia.googlesource.com/skia.git C:\skia
    echo          cd C:\skia
    echo          python tools\git-sync-deps
    echo          bin\gn gen out\Release --args="is_official_build=true skia_use_vulkan=true skia_use_gl=false"
    echo          ninja -C out\Release skia skshaper
    echo.
    exit /b 1
)
echo %GRN%[  OK  ]%RST% Skia: %SKIA_DIR%

:: ============================================================
:: 2. BUILD-VERZEICHNIS
:: ============================================================
echo.
echo %CYN%[2/5]%RST% Build-Verzeichnis vorbereiten...

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo %YLW%[ WARN ]%RST% Loesche: %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%"
    )
)

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
echo %GRN%[  OK  ]%RST% Build-Dir: %BUILD_DIR%

:: ============================================================
:: 3. CMAKE KONFIGURIEREN
:: ============================================================
echo.
echo %CYN%[3/5]%RST% CMake konfigurieren (%COMPILER_INFO%)...

set "CMAKE_CMD=cmake"
set "CMAKE_CMD=%CMAKE_CMD% -S "%SCRIPT_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -B "%BUILD_DIR%""
if defined GENERATOR set "CMAKE_CMD=%CMAKE_CMD% -G "%GENERATOR%""
if "%GENERATOR%"=="Visual Studio 17 2022" set "CMAKE_CMD=%CMAKE_CMD% -A x64"
set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
set "CMAKE_CMD=%CMAKE_CMD% -DSKIA_DIR="%SKIA_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -DSKIA_LIB_DIR="%SKIA_DIR%\out\Release""
set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_INSTALL_PREFIX="%DIST_DIR%""
set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
if defined CMAKE_EXTRA_ARGS set "CMAKE_CMD=%CMAKE_CMD% %CMAKE_EXTRA_ARGS%"
if defined Qt6_DIR set "CMAKE_CMD=%CMAKE_CMD% -DQt6_DIR="%Qt6_DIR%""
if defined VULKAN_SDK set "CMAKE_CMD=%CMAKE_CMD% -DCMAKE_PREFIX_PATH="%VULKAN_SDK%""

echo     %CMAKE_CMD%
echo.
%CMAKE_CMD%
if errorlevel 1 (
    echo.
    echo %RED%[FEHLER]%RST% CMake-Konfiguration fehlgeschlagen.
    echo        Haeufige Ursachen:
    echo          - Qt6_DIR nicht gesetzt
    echo          - Vulkan SDK nicht installiert
    echo          - Skia-Library nicht gefunden
    exit /b 1
)
echo %GRN%[  OK  ]%RST% CMake-Konfiguration erfolgreich

:: compile_commands.json in Root kopieren (fuer clangd)
if exist "%BUILD_DIR%\compile_commands.json" (
    copy /y "%BUILD_DIR%\compile_commands.json" "%SCRIPT_DIR%\compile_commands.json" >nul 2>&1
)

:: ============================================================
:: 4. BAUEN
:: ============================================================
echo.
echo %CYN%[4/5]%RST% Baue VulkanBrowser (%JOBS% Jobs, %BUILD_TYPE%)...
echo.

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS%
if errorlevel 1 (
    echo.
    echo %RED%[FEHLER]%RST% Build fehlgeschlagen.
    echo        Siehe Fehlermeldungen oben.
    exit /b 1
)

:: Binary-Pfad je nach Generator anpassen
if exist "%BUILD_DIR%\%BUILD_TYPE%\vulkan-browser.exe" (
    set "BINARY=%BUILD_DIR%\%BUILD_TYPE%\vulkan-browser.exe"
) else if exist "%BUILD_DIR%\vulkan-browser.exe" (
    set "BINARY=%BUILD_DIR%\vulkan-browser.exe"
) else (
    echo %RED%[FEHLER]%RST% vulkan-browser.exe nicht gefunden nach Build.
    exit /b 1
)

:: Groesse ermitteln
for %%f in ("%BINARY%") do set "BIN_SIZE=%%~zf"
set /a "BIN_SIZE_KB=%BIN_SIZE% / 1024"
set /a "BIN_SIZE_MB=%BIN_SIZE_KB% / 1024"

echo.
echo %GRN%[  OK  ]%RST% Binary: %BINARY%
echo %GRN%[  OK  ]%RST% Groesse: %BIN_SIZE_MB% MB (%BIN_SIZE_KB% KB)

:: ============================================================
:: 5. STARTEN
:: ============================================================
echo.
echo %CYN%[5/5]%RST% Starte VulkanBrowser...

if "%NO_RUN%"=="1" (
    echo %YLW%[ SKIP ]%RST% --no-run gesetzt, Browser wird nicht gestartet.
    goto done
)

:: Validierungslayer im Debug-Build aktivieren
if "%BUILD_TYPE%"=="Debug" (
    echo %YLW%[ INFO ]%RST% Debug-Build: Vulkan Validation Layers aktiv
    set "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
)

:: Qt-Plugins-Pfad setzen (Qt findet seine DLLs sonst nicht)
if defined Qt6_DIR (
    for %%d in ("%Qt6_DIR%\..\..\..") do set "QT_PLUGIN_PATH=%%~fd\plugins"
    if exist "!QT_PLUGIN_PATH!" (
        echo %GRN%[  OK  ]%RST% Qt-Plugins: !QT_PLUGIN_PATH!
    )
)

:: Vulkan SDK Layer-Pfad
if defined VULKAN_SDK (
    set "VK_LAYER_PATH=%VULKAN_SDK%\Bin"
)

echo.
echo %BLD%%GRN%============================================================%RST%
echo %BLD%%GRN%   Build erfolgreich! Browser startet...%RST%
echo %BLD%%GRN%============================================================%RST%
echo.
echo   Binary:   %BINARY%
echo   Build:    %BUILD_TYPE%
echo   Compiler: %COMPILER_INFO%
echo   Skia:     %SKIA_DIR%
echo.
echo   [Fenster schliessen oder Strg+C um den Browser zu beenden]
echo.

start "" "%BINARY%"
if errorlevel 1 (
    echo %RED%[FEHLER]%RST% Browser konnte nicht gestartet werden.
    echo        Starte manuell: "%BINARY%"
    exit /b 1
)

:done
echo.
echo %GRN%Fertig.%RST%
endlocal
exit /b 0

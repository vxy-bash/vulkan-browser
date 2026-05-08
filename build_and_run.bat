@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion
title VulkanBrowser - Build und Start

:: ============================================================
:: build_and_run.bat
:: Kompiliert VulkanBrowser und startet ihn danach direkt.
:: Fehlende Tools (CMake, Ninja, VS Build Tools) werden
:: automatisch per winget installiert wenn moeglich.
::
:: Verwendung:
::   build_and_run.bat                       Release-Build + Start
::   build_and_run.bat --debug               Debug-Build + Start
::   build_and_run.bat --clean               Build-Ordner loeschen
::   build_and_run.bat --skia-dir C:\skia    Skia-Pfad angeben
::   build_and_run.bat --no-run              Nur bauen, nicht starten
::   build_and_run.bat --jobs 8              Parallel-Jobs
:: ============================================================

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
set "BINARY="

:: ---------- Argumente parsen ----------
:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="--debug"    ( set "BUILD_TYPE=Debug"   & shift & goto parse_args )
if /i "%~1"=="--clean"    ( set "CLEAN=1"             & shift & goto parse_args )
if /i "%~1"=="--no-run"   ( set "NO_RUN=1"            & shift & goto parse_args )
if /i "%~1"=="--jobs"     ( set "JOBS=%~2"            & shift & shift & goto parse_args )
if /i "%~1"=="--skia-dir" ( set "SKIA_DIR_ARG=%~2"   & shift & shift & goto parse_args )
if /i "%~1"=="--help"     goto show_help
echo [FEHLER] Unbekanntes Argument: %~1
goto :fatal

:show_help
echo.
echo  build_and_run.bat [Optionen]
echo.
echo  --debug             Debug-Build mit Validation Layers
echo  --clean             Build-Ordner loeschen und neu bauen
echo  --no-run            Nur bauen, Browser nicht starten
echo  --skia-dir ^<Pfad^>   Pfad zum vorgebauten Skia-Verzeichnis
echo  --jobs ^<N^>          Parallel-Jobs (Standard: CPU-Kerne)
echo  --help              Diese Hilfe anzeigen
echo.
pause
exit /b 0

:args_done

echo.
echo ============================================================
echo    VulkanBrowser - Build und Start   [%BUILD_TYPE%]
echo ============================================================
echo.

:: ============================================================
:: 1. VORAUSSETZUNGEN PRUEFEN UND INSTALLIEREN
:: ============================================================
echo [1/5] Pruefe Voraussetzungen...
echo.

:: --- CMake ---
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ WARN ] CMake nicht gefunden - versuche automatische Installation...
    where winget >nul 2>&1
    if errorlevel 1 (
        echo [FEHLER] winget nicht verfuegbar.
        echo         Bitte CMake manuell installieren:
        echo         https://cmake.org/download/
        goto :fatal
    )
    echo         Installiere CMake via winget...
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
    if errorlevel 1 (
        echo [FEHLER] CMake-Installation fehlgeschlagen.
        echo         Bitte manuell installieren: https://cmake.org/download/
        goto :fatal
    )
    :: PATH aktualisieren
    set "PATH=%PATH%;C:\Program Files\CMake\bin"
    where cmake >nul 2>&1
    if errorlevel 1 (
        echo [FEHLER] CMake installiert aber nicht im PATH.
        echo         Bitte CMD neu starten und nochmal ausfuehren.
        goto :fatal
    )
    echo [  OK  ] CMake erfolgreich installiert.
)
for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "version"') do set "CMAKE_VER=%%v"
echo [  OK  ] CMake %CMAKE_VER%

:: --- Vulkan SDK ---
if defined VULKAN_SDK (
    echo [  OK  ] Vulkan SDK: %VULKAN_SDK%
) else (
    where vkconfig >nul 2>&1
    if not errorlevel 1 (
        echo [  OK  ] Vulkan SDK im PATH gefunden
    ) else (
        echo [ WARN ] VULKAN_SDK nicht gesetzt.
        echo          Falls der Build fehlschlaegt, Vulkan SDK installieren:
        echo          https://vulkan.lunarg.com/sdk/home
        echo          Oder: winget install KhronosGroup.VulkanSDK
    )
)

:: --- Compiler suchen: VS 2022 -> Clang -> MinGW ---
set "GENERATOR="
set "COMPILER_INFO="
set "CMAKE_EXTRA_ARGS="

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
    echo [  OK  ] Compiler: Visual Studio 2022
    goto compiler_found
)

where clang++ >nul 2>&1
if not errorlevel 1 (
    for /f "tokens=3" %%v in ('clang++ --version 2^>^&1 ^| findstr /i "version"') do set "CLANG_VER=%%v"
    set "GENERATOR=Ninja"
    set "COMPILER_INFO=Clang++ !CLANG_VER!"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang"
    echo [  OK  ] Compiler: Clang++ !CLANG_VER!
    goto compiler_found
)

where g++ >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=MinGW Makefiles"
    set "COMPILER_INFO=MinGW g++"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc"
    echo [  OK  ] Compiler: MinGW g++
    goto compiler_found
)

:: Kein Compiler - versuche VS Build Tools via winget
echo [ WARN ] Kein Compiler gefunden - installiere Visual Studio Build Tools...
where winget >nul 2>&1
if errorlevel 1 (
    echo [FEHLER] Kein Compiler und kein winget gefunden.
    echo         Bitte Visual Studio 2022 manuell installieren:
    echo         https://visualstudio.microsoft.com/
    goto :fatal
)
winget install --id Microsoft.VisualStudio.2022.BuildTools ^
    --silent --accept-package-agreements --accept-source-agreements ^
    --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
if errorlevel 1 (
    echo [FEHLER] Installation der Build Tools fehlgeschlagen.
    echo         Bitte manuell: https://visualstudio.microsoft.com/
    goto :fatal
)
echo [  OK  ] VS Build Tools installiert - bitte CMD neu starten.
echo         Dann build_and_run.bat erneut ausfuehren.
goto :fatal

:compiler_found

:: --- Ninja (wenn Clang/MinGW gewaehlt) ---
if "%GENERATOR%"=="Ninja" (
    where ninja >nul 2>&1
    if errorlevel 1 (
        echo [ WARN ] Ninja nicht gefunden - installiere via winget...
        winget install --id Ninja-build.Ninja --silent --accept-package-agreements --accept-source-agreements >nul 2>&1
        set "PATH=%PATH%;%LOCALAPPDATA%\Microsoft\WinGet\Packages\Ninja-build.Ninja_Microsoft.Winget.Source_8wekyb3d8bbwe"
        where ninja >nul 2>&1
        if errorlevel 1 (
            echo [ WARN ] Ninja nicht im PATH - verwende NMake als Fallback.
            set "GENERATOR=NMake Makefiles"
        ) else (
            echo [  OK  ] Ninja installiert.
        )
    ) else (
        echo [  OK  ] Ninja gefunden
    )
)

:: --- Qt6 ---
if defined Qt6_DIR (
    echo [  OK  ] Qt6_DIR: %Qt6_DIR%
) else if defined QTDIR (
    set "Qt6_DIR=%QTDIR%\lib\cmake\Qt6"
    echo [  OK  ] Qt6 via QTDIR: %QTDIR%
) else (
    echo [ WARN ] Qt6_DIR nicht gesetzt.
    echo          Beispiel: set Qt6_DIR=C:\Qt\6.7.0\msvc2022_64\lib\cmake\Qt6
    echo          Download:  https://www.qt.io/download-open-source
)

:: --- Skia ---
if defined SKIA_DIR_ARG set "SKIA_DIR=%SKIA_DIR_ARG%"
if not defined SKIA_DIR (
    set "SKIA_DIR=%SCRIPT_DIR%\third_party\skia"
    echo [ WARN ] SKIA_DIR nicht gesetzt - suche in %SKIA_DIR%
)

if not exist "%SKIA_DIR%\out\Release\skia.lib" (
    echo.
    echo [FEHLER] Skia nicht gefunden: %SKIA_DIR%\out\Release\skia.lib
    echo.
    echo          Skia muss einmalig manuell gebaut werden:
    echo.
    echo          1. git clone https://skia.googlesource.com/skia.git C:\skia
    echo          2. cd C:\skia
    echo          3. python tools\git-sync-deps
    echo          4. bin\gn gen out\Release --args="is_official_build=true skia_use_vulkan=true skia_use_gl=false"
    echo          5. ninja -C out\Release skia skshaper
    echo.
    echo          Dann: build_and_run.bat --skia-dir C:\skia
    echo.
    goto :fatal
)
echo [  OK  ] Skia: %SKIA_DIR%

:: ============================================================
:: 2. BUILD-VERZEICHNIS
:: ============================================================
echo.
echo [2/5] Build-Verzeichnis vorbereiten...

if "%CLEAN%"=="1" (
    if exist "%BUILD_DIR%" (
        echo [ WARN ] Loesche: %BUILD_DIR%
        rmdir /s /q "%BUILD_DIR%"
    )
)
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
echo [  OK  ] %BUILD_DIR%

:: ============================================================
:: 3. CMAKE KONFIGURIEREN
:: ============================================================
echo.
echo [3/5] CMake konfigurieren (%COMPILER_INFO%)...
echo.

set "CMAKE_ARGS=-S "%SCRIPT_DIR%" -B "%BUILD_DIR%""
if defined GENERATOR                        set "CMAKE_ARGS=%CMAKE_ARGS% -G "%GENERATOR%""
if "%GENERATOR%"=="Visual Studio 17 2022"  set "CMAKE_ARGS=%CMAKE_ARGS% -A x64"
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=%BUILD_TYPE%"
set "CMAKE_ARGS=%CMAKE_ARGS% -DSKIA_DIR="%SKIA_DIR%""
set "CMAKE_ARGS=%CMAKE_ARGS% -DSKIA_LIB_DIR="%SKIA_DIR%\out\Release""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_INSTALL_PREFIX="%DIST_DIR%""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
if defined CMAKE_EXTRA_ARGS                 set "CMAKE_ARGS=%CMAKE_ARGS% %CMAKE_EXTRA_ARGS%"
if defined Qt6_DIR                          set "CMAKE_ARGS=%CMAKE_ARGS% -DQt6_DIR="%Qt6_DIR%""
if defined VULKAN_SDK                       set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH="%VULKAN_SDK%""

cmake %CMAKE_ARGS%
if errorlevel 1 (
    echo.
    echo [FEHLER] CMake-Konfiguration fehlgeschlagen.
    echo         Haeufige Ursachen:
    echo           - Qt6_DIR nicht gesetzt oder falsch
    echo           - Vulkan SDK fehlt
    echo           - Skia-Pfad falsch
    goto :fatal
)
echo.
echo [  OK  ] CMake-Konfiguration erfolgreich.

if exist "%BUILD_DIR%\compile_commands.json" (
    copy /y "%BUILD_DIR%\compile_commands.json" "%SCRIPT_DIR%\compile_commands.json" >nul 2>&1
)

:: ============================================================
:: 4. BAUEN
:: ============================================================
echo.
echo [4/5] Baue VulkanBrowser (%JOBS% Jobs)...
echo.

cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS%
if errorlevel 1 (
    echo.
    echo [FEHLER] Build fehlgeschlagen - Fehler siehe oben.
    goto :fatal
)

:: Binary lokalisieren
if exist "%BUILD_DIR%\%BUILD_TYPE%\vulkan-browser.exe" set "BINARY=%BUILD_DIR%\%BUILD_TYPE%\vulkan-browser.exe"
if exist "%BUILD_DIR%\vulkan-browser.exe"              set "BINARY=%BUILD_DIR%\vulkan-browser.exe"
if not defined BINARY (
    echo [FEHLER] vulkan-browser.exe nach dem Build nicht gefunden.
    goto :fatal
)

for %%f in ("%BINARY%") do set "BIN_SIZE=%%~zf"
set /a "BIN_MB=%BIN_SIZE% / 1048576"
echo.
echo [  OK  ] Binary: %BINARY%  (%BIN_MB% MB)

:: ============================================================
:: 5. STARTEN
:: ============================================================
echo.
echo [5/5] Starte VulkanBrowser...

if "%NO_RUN%"=="1" (
    echo [ SKIP ] --no-run gesetzt, Browser wird nicht gestartet.
    goto :done
)

if /i "%BUILD_TYPE%"=="Debug" (
    echo [ INFO ] Debug: Vulkan Validation Layers aktiv
    set "VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation"
)
if defined Qt6_DIR (
    for %%d in ("%Qt6_DIR%\..\..\..") do set "QT_PLUGIN_PATH=%%~fd\plugins"
)
if defined VULKAN_SDK set "VK_LAYER_PATH=%VULKAN_SDK%\Bin"

echo.
echo ============================================================
echo   Build erfolgreich - Browser laeuft.
echo ============================================================
echo.
echo   Binary  : %BINARY%
echo   Build   : %BUILD_TYPE%
echo   Compiler: %COMPILER_INFO%
echo.
echo   Fenster bleibt offen bis Browser geschlossen wird.
echo   Strg+C beendet Browser und Fenster sofort.
echo.

"%BINARY%"
set "EXIT_CODE=%errorlevel%"

echo.
if "%EXIT_CODE%"=="0" (
    echo [  OK  ] Browser normal beendet.
) else (
    echo [FEHLER] Browser beendet mit Exit-Code %EXIT_CODE%
    if "%EXIT_CODE%"=="1" echo         Kein Vulkan-faehiges GPU gefunden.
    if "%EXIT_CODE%"=="3" echo         Vulkan-Treiber abgestuerzt.
)

:done
echo.
echo Druecke eine beliebige Taste zum Schliessen...
pause >nul
endlocal
exit /b 0

:: ============================================================
:fatal
echo.
echo ======================================================
echo   BUILD FEHLGESCHLAGEN - Lies die Meldung oben.
echo ======================================================
echo.
echo Druecke eine beliebige Taste zum Schliessen...
pause >nul
endlocal
exit /b 1

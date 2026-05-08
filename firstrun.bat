@echo off
chcp 65001 >nul 2>&1
setlocal EnableDelayedExpansion
title VulkanBrowser - Ersteinrichtung

:: ============================================================
:: firstrun.bat
:: Richtet alles fuer den ersten Start ein:
::   1. Winget pruefen / aktivieren
::   2. Git installieren
::   3. CMake installieren
::   4. Python installieren
::   5. Vulkan SDK installieren
::   6. Visual Studio Build Tools installieren
::   7. Ninja installieren
::   8. Qt6 installieren
::   9. depot_tools holen
::  10. Skia klonen und bauen (Vulkan-only)
::  11. VulkanBrowser bauen
::  12. VulkanBrowser starten
:: ============================================================

set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "TOOLS_DIR=%SCRIPT_DIR%\third_party"
set "SKIA_DIR=%TOOLS_DIR%\skia"
set "DEPOT_DIR=%TOOLS_DIR%\depot_tools"
set "LOG_FILE=%SCRIPT_DIR%\firstrun.log"
set "BUILD_DIR=%SCRIPT_DIR%\build"

:: Qt-Version die per winget verfuegbar ist
set "QT_WINGET_ID=TheQtCompany.QtDesignStudio"
:: Wir nutzen den offiziellen Qt-Installer-Pfad
set "QT_DEFAULT=C:\Qt\6.7.0\msvc2022_64"

echo.
echo ============================================================
echo   VulkanBrowser - Ersteinrichtung
echo   Alle Ausgaben werden auch in firstrun.log gespeichert.
echo ============================================================
echo.

:: Log-Datei anlegen
echo VulkanBrowser Ersteinrichtung - %DATE% %TIME% > "%LOG_FILE%"

:: ============================================================
:: Hilfsfunktionen als Inline-Labels
:: ============================================================

:: Pruefe ob als Admin ausgefuehrt (einige Installer benoetigen das)
net session >nul 2>&1
if errorlevel 1 (
    echo [ WARN ] Nicht als Administrator ausgefuehrt.
    echo          Manche Installer benoetigen Admin-Rechte.
    echo          Bei Problemen: Rechtsklick auf firstrun.bat
    echo          dann "Als Administrator ausfuehren".
    echo.
)

:: ============================================================
:: SCHRITT 1 - WINGET
:: ============================================================
echo [1/12] Pruefe winget...

where winget >nul 2>&1
if errorlevel 1 (
    echo [ WARN ] winget nicht gefunden.
    echo          winget ist ab Windows 10 1809 verfuegbar.
    echo          Update: ms-windows-store://pdp/?ProductId=9NBLGGH4NNS1
    echo          Oder manuell aus dem Microsoft Store installieren.
    echo.
    echo          Ohne winget muessen alle Tools manuell installiert werden.
    echo          Druecke eine Taste um trotzdem fortzufahren...
    pause >nul
) else (
    for /f "tokens=*" %%v in ('winget --version 2^>^&1') do set "WINGET_VER=%%v"
    echo [  OK  ] winget !WINGET_VER!
    :: Quellen aktualisieren (einmalig, still)
    winget source update >nul 2>&1
)
echo.

:: ============================================================
:: SCHRITT 2 - GIT
:: ============================================================
echo [2/12] Pruefe Git...

where git >nul 2>&1
if errorlevel 1 (
    echo [ INST ] Git nicht gefunden - installiere...
    winget install --id Git.Git --silent --accept-package-agreements --accept-source-agreements
    if errorlevel 1 goto :git_manual
    :: PATH fuer diese Session aktualisieren
    set "PATH=%PATH%;C:\Program Files\Git\cmd"
    where git >nul 2>&1
    if errorlevel 1 goto :git_manual
    echo [  OK  ] Git installiert.
    goto :git_done
    :git_manual
    echo [FEHLER] Git-Installation fehlgeschlagen.
    echo          Bitte manuell: https://git-scm.com/download/win
    goto :fatal
)
for /f "tokens=3" %%v in ('git --version 2^>^&1') do set "GIT_VER=%%v"
echo [  OK  ] Git %GIT_VER%
:git_done
echo.

:: ============================================================
:: SCHRITT 3 - CMAKE
:: ============================================================
echo [3/12] Pruefe CMake...

where cmake >nul 2>&1
if errorlevel 1 (
    echo [ INST ] CMake nicht gefunden - installiere...
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
    if errorlevel 1 (
        echo [FEHLER] CMake-Installation fehlgeschlagen.
        echo          Manuell: https://cmake.org/download/
        goto :fatal
    )
    set "PATH=%PATH%;C:\Program Files\CMake\bin"
    where cmake >nul 2>&1
    if errorlevel 1 (
        echo [ WARN ] CMake installiert aber noch nicht im PATH.
        echo          Bitte CMD neu starten und firstrun.bat erneut starten.
        goto :restart_required
    )
    echo [  OK  ] CMake installiert.
) else (
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "version"') do set "CMAKE_VER=%%v"
    echo [  OK  ] CMake %CMAKE_VER%
)
echo.

:: ============================================================
:: SCHRITT 4 - PYTHON
:: ============================================================
echo [4/12] Pruefe Python...

where python >nul 2>&1
if errorlevel 1 (
    echo [ INST ] Python nicht gefunden - installiere...
    winget install --id Python.Python.3.12 --silent --accept-package-agreements --accept-source-agreements
    if errorlevel 1 (
        echo [FEHLER] Python-Installation fehlgeschlagen.
        echo          Manuell: https://python.org/downloads/
        goto :fatal
    )
    set "PATH=%PATH%;%LOCALAPPDATA%\Programs\Python\Python312;%LOCALAPPDATA%\Programs\Python\Python312\Scripts"
    where python >nul 2>&1
    if errorlevel 1 (
        echo [ WARN ] Python installiert aber noch nicht im PATH.
        echo          Bitte CMD neu starten und firstrun.bat erneut starten.
        goto :restart_required
    )
    echo [  OK  ] Python installiert.
) else (
    for /f "tokens=2" %%v in ('python --version 2^>^&1') do set "PY_VER=%%v"
    echo [  OK  ] Python %PY_VER%
)
echo.

:: ============================================================
:: SCHRITT 5 - VULKAN SDK
:: ============================================================
echo [5/12] Pruefe Vulkan SDK...

if defined VULKAN_SDK (
    echo [  OK  ] Vulkan SDK: %VULKAN_SDK%
) else (
    where vkconfig >nul 2>&1
    if not errorlevel 1 (
        echo [  OK  ] Vulkan SDK im PATH
    ) else (
        echo [ INST ] Vulkan SDK nicht gefunden - installiere...
        winget install --id KhronosGroup.VulkanSDK --silent --accept-package-agreements --accept-source-agreements
        if errorlevel 1 (
            echo [ WARN ] Automatische Installation fehlgeschlagen.
            echo          Bitte manuell installieren:
            echo          https://vulkan.lunarg.com/sdk/home
            echo          Dann CMD neu starten und firstrun.bat erneut starten.
            pause
        ) else (
            echo [  OK  ] Vulkan SDK installiert.
            echo [ WARN ] Bitte CMD neu starten damit VULKAN_SDK gesetzt wird.
            goto :restart_required
        )
    )
)
echo.

:: ============================================================
:: SCHRITT 6 - VISUAL STUDIO BUILD TOOLS / MSVC
:: ============================================================
echo [6/12] Pruefe C++ Compiler...

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" set "VSWHERE=%ProgramFiles%\Microsoft Visual Studio\Installer\vswhere.exe"

set "VS_FOUND=0"
if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (
        `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`
    ) do (
        set "VS_PATH=%%i"
        set "VS_FOUND=1"
    )
)

if "%VS_FOUND%"=="1" (
    echo [  OK  ] Visual Studio C++ gefunden: %VS_PATH%
) else (
    where clang++ >nul 2>&1
    if not errorlevel 1 (
        echo [  OK  ] Clang++ gefunden
    ) else (
        echo [ INST ] Kein C++ Compiler - installiere VS 2022 Build Tools...
        echo          (Das kann 5-10 Minuten dauern)
        winget install --id Microsoft.VisualStudio.2022.BuildTools ^
            --silent --accept-package-agreements --accept-source-agreements ^
            --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
        if errorlevel 1 (
            echo [FEHLER] Build Tools Installation fehlgeschlagen.
            echo          Manuell: https://visualstudio.microsoft.com/downloads/
            echo          "Tools fuer Visual Studio" -> "Build Tools fuer Visual Studio 2022"
            goto :fatal
        )
        echo [  OK  ] VS 2022 Build Tools installiert.
        echo [ WARN ] Bitte CMD neu starten und firstrun.bat erneut starten.
        goto :restart_required
    )
)
echo.

:: ============================================================
:: SCHRITT 7 - NINJA
:: ============================================================
echo [7/12] Pruefe Ninja...

where ninja >nul 2>&1
if errorlevel 1 (
    echo [ INST ] Ninja nicht gefunden - installiere...
    winget install --id Ninja-build.Ninja --silent --accept-package-agreements --accept-source-agreements
    if errorlevel 1 (
        echo [ WARN ] Ninja-Installation fehlgeschlagen - Build nutzt NMake als Fallback.
    ) else (
        echo [  OK  ] Ninja installiert.
    )
) else (
    echo [  OK  ] Ninja gefunden
)
echo.

:: ============================================================
:: SCHRITT 8 - QT6
:: ============================================================
echo [8/12] Pruefe Qt6...

set "QT_FOUND=0"
if defined Qt6_DIR      set "QT_FOUND=1"
if defined QTDIR        set "QT_FOUND=1"
if exist "%QT_DEFAULT%" set "QT_FOUND=1"

:: Haeufige Qt-Installationspfade absuchen
for %%p in (
    "C:\Qt\6.7.0\msvc2022_64"
    "C:\Qt\6.6.0\msvc2022_64"
    "C:\Qt\6.5.0\msvc2022_64"
    "C:\Qt\6.7.0\mingw_64"
    "C:\Qt\6.6.0\mingw_64"
) do (
    if exist "%%~p\lib\cmake\Qt6" (
        set "Qt6_DIR=%%~p\lib\cmake\Qt6"
        set "QT_FOUND=1"
    )
)

if "%QT_FOUND%"=="1" (
    if defined Qt6_DIR (
        echo [  OK  ] Qt6 gefunden: %Qt6_DIR%
    ) else (
        echo [  OK  ] Qt6 gefunden (QTDIR/Standardpfad)
    )
) else (
    echo [ INST ] Qt6 nicht gefunden.
    echo          Qt6 kann nicht automatisch per winget installiert werden
    echo          (der offizielle Installer erfordert einen Qt-Account).
    echo.
    echo          Bitte Qt6 manuell installieren:
    echo          1. https://www.qt.io/download-open-source
    echo          2. Installer starten
    echo          3. Qt 6.x -> MSVC 2019 64-bit (oder MinGW 64-bit) auswaehlen
    echo          4. Dann Qt6_DIR setzen:
    echo             set Qt6_DIR=C:\Qt\6.x.x\msvc2022_64\lib\cmake\Qt6
    echo          5. firstrun.bat erneut starten
    echo.
    echo          Druecke eine Taste um trotzdem fortzufahren
    echo          (Build wird spaeter fehlschlagen falls Qt6 fehlt)...
    pause >nul
)
echo.

:: ============================================================
:: SCHRITT 9 - DEPOT_TOOLS (fuer Skia-Build benoetigt)
:: ============================================================
echo [9/12] Pruefe depot_tools...

if not exist "%DEPOT_DIR%" (
    echo [ INST ] Klone depot_tools...
    if not exist "%TOOLS_DIR%" mkdir "%TOOLS_DIR%"
    git clone --depth=1 https://chromium.googlesource.com/chromium/tools/depot_tools.git "%DEPOT_DIR%"
    if errorlevel 1 (
        echo [FEHLER] depot_tools konnte nicht geklont werden.
        echo          Internetverbindung pruefen.
        goto :fatal
    )
    echo [  OK  ] depot_tools geklont.
) else (
    echo [  OK  ] depot_tools vorhanden: %DEPOT_DIR%
)
set "PATH=%DEPOT_DIR%;%PATH%"
echo.

:: ============================================================
:: SCHRITT 10 - SKIA KLONEN UND BAUEN
:: ============================================================
echo [10/12] Pruefe Skia...

if exist "%SKIA_DIR%\out\Release\skia.lib" (
    echo [  OK  ] Skia bereits gebaut: %SKIA_DIR%\out\Release\skia.lib
    goto :skia_done
)

if not exist "%SKIA_DIR%" (
    echo [ INST ] Klone Skia (ca. 500 MB)...
    git clone --depth=1 https://skia.googlesource.com/skia.git "%SKIA_DIR%"
    if errorlevel 1 (
        echo [FEHLER] Skia konnte nicht geklont werden.
        echo          Internetverbindung oder Firewall pruefen.
        goto :fatal
    )
    echo [  OK  ] Skia geklont.
) else (
    echo [  OK  ] Skia-Quellen vorhanden, ueberspringe Klon.
)

echo.
echo [ INST ] Synchronisiere Skia-Abhaengigkeiten...
echo          (Laedt weitere Abhaengigkeiten - kann einige Minuten dauern)
pushd "%SKIA_DIR%"
python tools\git-sync-deps 2>&1
if errorlevel 1 (
    popd
    echo [FEHLER] git-sync-deps fehlgeschlagen.
    goto :fatal
)
echo [  OK  ] Abhaengigkeiten synchronisiert.

echo.
echo [ INST ] Konfiguriere Skia-Build (Vulkan-only, kein GL/EGL)...
bin\gn gen out\Release --args="is_official_build=true skia_use_vulkan=true skia_use_gl=false skia_use_egl=false skia_use_x11=false skia_enable_skottie=false cc=\"clang\" cxx=\"clang++\""
if errorlevel 1 (
    :: Fallback: MSVC statt clang
    echo [ WARN ] Clang nicht gefunden - versuche MSVC-Konfiguration...
    bin\gn gen out\Release --args="is_official_build=true skia_use_vulkan=true skia_use_gl=false skia_use_egl=false skia_enable_skottie=false"
    if errorlevel 1 (
        popd
        echo [FEHLER] Skia gn-Konfiguration fehlgeschlagen.
        goto :fatal
    )
)
echo [  OK  ] Skia konfiguriert.

echo.
echo [ INST ] Baue Skia mit %JOBS% Jobs (kann 5-15 Minuten dauern)...
echo          Bitte warten...
ninja -C out\Release -j%JOBS% skia skshaper 2>&1
if errorlevel 1 (
    popd
    echo [FEHLER] Skia-Build fehlgeschlagen.
    echo          Fehlermeldung oben lesen.
    goto :fatal
)
popd
echo [  OK  ] Skia erfolgreich gebaut.

:skia_done
echo.

:: ============================================================
:: SCHRITT 11 - VULKAN BROWSER BAUEN
:: ============================================================
echo [11/12] Baue VulkanBrowser...
echo.

if "%CLEAN%"=="" set "CLEAN=0"
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

:: Compiler erneut ermitteln
set "GENERATOR="
set "CMAKE_EXTRA_ARGS="
set "COMPILER_INFO="

if exist "%VSWHERE%" (
    for /f "usebackq tokens=*" %%i in (
        `"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath 2^>nul`
    ) do set "VS_PATH=%%i"
)
if defined VS_PATH (
    set "GENERATOR=Visual Studio 17 2022"
    set "COMPILER_INFO=MSVC"
    goto :cmake_go
)
where clang++ >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=Ninja"
    set "COMPILER_INFO=Clang++"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang"
    goto :cmake_go
)
where g++ >nul 2>&1
if not errorlevel 1 (
    set "GENERATOR=MinGW Makefiles"
    set "COMPILER_INFO=MinGW g++"
    set "CMAKE_EXTRA_ARGS=-DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc"
    goto :cmake_go
)
echo [FEHLER] Kein Compiler gefunden - Schritt 6 hat nicht funktioniert.
goto :fatal

:cmake_go
set "CMAKE_ARGS=-S "%SCRIPT_DIR%" -B "%BUILD_DIR%""
if defined GENERATOR                       set "CMAKE_ARGS=%CMAKE_ARGS% -G "%GENERATOR%""
if "%GENERATOR%"=="Visual Studio 17 2022" set "CMAKE_ARGS=%CMAKE_ARGS% -A x64"
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_BUILD_TYPE=Release"
set "CMAKE_ARGS=%CMAKE_ARGS% -DSKIA_DIR="%SKIA_DIR%""
set "CMAKE_ARGS=%CMAKE_ARGS% -DSKIA_LIB_DIR="%SKIA_DIR%\out\Release""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_INSTALL_PREFIX="%SCRIPT_DIR%\dist""
set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
if defined CMAKE_EXTRA_ARGS                set "CMAKE_ARGS=%CMAKE_ARGS% %CMAKE_EXTRA_ARGS%"
if defined Qt6_DIR                         set "CMAKE_ARGS=%CMAKE_ARGS% -DQt6_DIR="%Qt6_DIR%""
if defined VULKAN_SDK                      set "CMAKE_ARGS=%CMAKE_ARGS% -DCMAKE_PREFIX_PATH="%VULKAN_SDK%""

cmake %CMAKE_ARGS%
if errorlevel 1 (
    echo.
    echo [FEHLER] CMake-Konfiguration fehlgeschlagen.
    goto :fatal
)

cmake --build "%BUILD_DIR%" --config Release --parallel %JOBS%
if errorlevel 1 (
    echo.
    echo [FEHLER] Build fehlgeschlagen.
    goto :fatal
)

set "BINARY="
if exist "%BUILD_DIR%\Release\vulkan-browser.exe" set "BINARY=%BUILD_DIR%\Release\vulkan-browser.exe"
if exist "%BUILD_DIR%\vulkan-browser.exe"         set "BINARY=%BUILD_DIR%\vulkan-browser.exe"
if not defined BINARY (
    echo [FEHLER] vulkan-browser.exe nicht gefunden nach Build.
    goto :fatal
)

for %%f in ("%BINARY%") do set "BIN_SIZE=%%~zf"
set /a "BIN_MB=%BIN_SIZE% / 1048576"
echo.
echo [  OK  ] Binary: %BINARY%  (%BIN_MB% MB)
echo.

if exist "%BUILD_DIR%\compile_commands.json" (
    copy /y "%BUILD_DIR%\compile_commands.json" "%SCRIPT_DIR%\compile_commands.json" >nul 2>&1
)

:: ============================================================
:: SCHRITT 12 - STARTEN
:: ============================================================
echo [12/12] Starte VulkanBrowser...

if defined Qt6_DIR (
    for %%d in ("%Qt6_DIR%\..\..\..") do set "QT_PLUGIN_PATH=%%~fd\plugins"
)
if defined VULKAN_SDK set "VK_LAYER_PATH=%VULKAN_SDK%\Bin"

echo.
echo ============================================================
echo   Ersteinrichtung abgeschlossen - Browser laeuft.
echo ============================================================
echo.
echo   Binary    : %BINARY%
echo   Skia      : %SKIA_DIR%
echo   Compiler  : %COMPILER_INFO%
echo.
echo   Beim naechsten Mal reicht: build_and_run.bat
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

echo.
echo Druecke eine beliebige Taste zum Schliessen...
pause >nul
endlocal
exit /b 0

:: ============================================================
:restart_required
echo.
echo ============================================================
echo   Neustart erforderlich
echo ============================================================
echo.
echo   Ein oder mehrere Tools wurden installiert und sind
echo   erst nach einem CMD-Neustart verfuegbar.
echo.
echo   Bitte:
echo     1. Dieses Fenster schliessen
echo     2. Neue CMD oder neues Terminal oeffnen
echo     3. firstrun.bat erneut ausfuehren
echo.
echo Druecke eine beliebige Taste zum Schliessen...
pause >nul
endlocal
exit /b 0

:: ============================================================
:fatal
echo.
echo ======================================================
echo   FEHLER - Ersteinrichtung nicht abgeschlossen.
echo   Lies die Meldung oben.
echo ======================================================
echo.
echo   Log-Datei: %LOG_FILE%
echo.
echo Druecke eine beliebige Taste zum Schliessen...
pause >nul
endlocal
exit /b 1

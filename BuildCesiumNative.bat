@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  BuildCesiumNative.bat
REM  一键构建 cesium-native：vcpkg 准备 + CMake 编译 + 安装
REM  输出到 CesiumForUnreal/Source/ThirdParty/
REM  用法: BuildCesiumNative.bat [Debug|Release]
REM  只需运行一次，后续 UE 编译自动跳过。
REM ============================================================

set "PROJECT_DIR=%~dp0"
set "CESIUM_EXTERN=%PROJECT_DIR%Plugins\CesiumForUnreal\extern"
set "BUILD_DIR=%CESIUM_EXTERN%\build"
set "THIRDPARTY_DIR=%PROJECT_DIR%Plugins\CesiumForUnreal\Source\ThirdParty"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

REM ---- 检测是否已构建 ----
if exist "%THIRDPARTY_DIR%\include" (
    dir /s /b "%THIRDPARTY_DIR%\include\*.h" >nul 2>&1
    if !ERRORLEVEL! EQU 0 (
        echo cesium-native already built at: %THIRDPARTY_DIR%
        echo Delete that folder to force rebuild.
        pause
        exit /b 0
    )
)

echo ===== Cesium Native Build START =====
echo Project: %PROJECT_DIR%
echo Config:  %CONFIG%

REM ---- 1. 查找 UE 引擎目录 ----
if defined UE_ENGINE_DIR (
    if exist "%UE_ENGINE_DIR%\Engine\Build" (
        set "ENGINE_DIR=%UE_ENGINE_DIR%"
        goto :engine_found
    )
)
if exist "%PROJECT_DIR%..\UnrealEngine\Engine\Build" (
    set "ENGINE_DIR=%PROJECT_DIR%..\UnrealEngine"
    goto :engine_found
)
echo ERROR: Cannot find UnrealEngine.
echo Set UE_ENGINE_DIR or place UnrealEngine at same level as project.
pause
exit /b 1

:engine_found
echo Engine:  %ENGINE_DIR%

REM ---- 2. 查找 CMake ----
where cmake >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo cmake not in PATH, searching VS installation...
    for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath 2^>nul`) do (
        set "VS_CMAKE=%%i\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
    )
    if defined VS_CMAKE (
        set "PATH=!VS_CMAKE!;!PATH!"
        echo Found VS CMake: !VS_CMAKE!
    ) else (
        echo ERROR: cmake not found. Install CMake 3.15+ or Visual Studio with C++ CMake tools.
        pause
        exit /b 1
    )
)

REM ---- 3. vcpkg 准备 ----
set "VCPKG_COMMIT=afc0a2e01ae104a2474216a2df0e8d78516fd5af"
set "VCPKG_CACHE=%BUILD_DIR%\.ezvcpkg"
set "VCPKG_DIR=%VCPKG_CACHE%\%VCPKG_COMMIT%"
set "EZVCPKG_BASEDIR=%VCPKG_CACHE%"

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

REM 清理残留 lock 文件
if exist "%VCPKG_DIR%.lock" (
    echo Removing stale vcpkg lock file...
    del "%VCPKG_DIR%.lock"
)

if not exist "%VCPKG_DIR%\README.md" (
    echo.
    echo [1/5] Cloning vcpkg...
    if not exist "%VCPKG_CACHE%" mkdir "%VCPKG_CACHE%"
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: git clone vcpkg failed. Check your network/proxy.
        pause
        exit /b 1
    )
    git -C "%VCPKG_DIR%" checkout %VCPKG_COMMIT%
) else (
    echo [1/5] vcpkg clone: already exists
)

if not exist "%VCPKG_DIR%\vcpkg.exe" (
    echo.
    echo [2/5] Bootstrapping vcpkg...
    call "%VCPKG_DIR%\bootstrap-vcpkg.bat"
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: vcpkg bootstrap failed. Check your network/proxy.
        pause
        exit /b 1
    )
) else (
    echo [2/5] vcpkg.exe: already exists
)

REM ---- 4. CMake Configure ----
echo.
echo [3/5] CMake Configure...
cmake -B "%BUILD_DIR%" -S "%CESIUM_EXTERN%" ^
    -A x64 ^
    -DUNREAL_ENGINE_ROOT="%ENGINE_DIR%"

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

REM ---- 5. CMake Build ----
echo.
echo [4/5] CMake Build (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake build failed.
    pause
    exit /b 1
)

REM ---- 6. CMake Install → Source/ThirdParty/ ----
echo.
echo [5/5] CMake Install...
cmake --install "%BUILD_DIR%" --config %CONFIG%

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake install failed.
    pause
    exit /b 1
)

echo.
echo ===== Cesium Native Build END =====
echo Output: Plugins\CesiumForUnreal\Source\ThirdParty\
echo   include\ — header files
echo   lib\     — static libraries (%CONFIG%)
echo.
echo Now you can build the UE project normally.
pause
endlocal
exit /b 0

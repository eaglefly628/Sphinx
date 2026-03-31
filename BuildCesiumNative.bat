@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM  BuildCesiumNative.bat
REM  构建 cesium-native 第三方库，输出到 CesiumForUnreal/Source/ThirdParty/
REM  用法: BuildCesiumNative.bat [Debug|Release]
REM ============================================================

set "PROJECT_DIR=%~dp0"
set "CESIUM_EXTERN=%PROJECT_DIR%Plugins\CesiumForUnreal\extern"
set "BUILD_DIR=%CESIUM_EXTERN%\build"
set "CONFIG=%~1"
if "%CONFIG%"=="" set "CONFIG=Release"

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
    echo ERROR: cmake not found in PATH. Please install CMake 3.15+.
    pause
    exit /b 1
)

REM ---- 3. CMake Configure ----
echo.
echo [1/3] CMake Configure...
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

cmake -B "%BUILD_DIR%" -S "%CESIUM_EXTERN%" ^
    -A x64 ^
    -DUNREAL_ENGINE_ROOT="%ENGINE_DIR%" ^
    -DCMAKE_BUILD_TYPE=%CONFIG%

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configure failed.
    pause
    exit /b 1
)

REM ---- 4. CMake Build ----
echo.
echo [2/3] CMake Build (%CONFIG%)...
cmake --build "%BUILD_DIR%" --config %CONFIG% --parallel

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake build failed.
    pause
    exit /b 1
)

REM ---- 5. CMake Install → Source/ThirdParty/ ----
echo.
echo [3/3] CMake Install...
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

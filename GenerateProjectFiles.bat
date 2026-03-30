@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "PROJECT_UPROJECT=%PROJECT_DIR%eaglewalk.uproject"

if not exist "%PROJECT_UPROJECT%" (
    echo Cannot find "%PROJECT_UPROJECT%".
    echo Please place this bat file in the EagleWalk root directory.
    pause
    exit /b 1
)

REM 1) 优先使用环境变量 UE_ENGINE_DIR
if defined UE_ENGINE_DIR (
    if exist "%UE_ENGINE_DIR%\GenerateProjectFiles.bat" (
        set "ENGINE_DIR=%UE_ENGINE_DIR%"
        goto :found
    )
)

REM 2) 尝试同级 UnrealEngine 目录
if exist "%PROJECT_DIR%..\UnrealEngine\GenerateProjectFiles.bat" (
    set "ENGINE_DIR=%PROJECT_DIR%..\UnrealEngine"
    goto :found
)

REM 3) 都没找到，提示用户设置环境变量
echo Cannot find UnrealEngine.
echo.
echo Please set the UE_ENGINE_DIR environment variable to your engine path:
echo   set UE_ENGINE_DIR=D:\YourPath\UnrealEngine
echo.
echo Or place UnrealEngine at the same level as this project.
pause
exit /b 1

:found
echo Using engine: %ENGINE_DIR%

pushd "%ENGINE_DIR%"
call "%ENGINE_DIR%\GenerateProjectFiles.bat" -project="%PROJECT_UPROJECT%"
set "ERRORLEVEL_SAVE=%ERRORLEVEL%"
popd

if %ERRORLEVEL_SAVE% NEQ 0 (
    echo Error code: %ERRORLEVEL_SAVE%.
    pause
    exit /b %ERRORLEVEL_SAVE%
)

echo eaglewalk project files have been regenerated.
pause
endlocal
exit /b 0

@echo off
setlocal

set "PROJECT_DIR=%~dp0"
set "PROJECT_UPROJECT=%PROJECT_DIR%eaglewalk.uproject"
set "ENGINE_DIR=%PROJECT_DIR%..\UnrealEngine"

if not exist "%PROJECT_UPROJECT%" (
    echo Cannot find "%PROJECT_UPROJECT%".
    echo Please place this bat file in the EagleWalk root directory.
    pause
    exit /b 1
)

if not exist "%ENGINE_DIR%\GenerateProjectFiles.bat" (
    echo Cannot find GenerateProjectFiles.bat in UnrealEngine:
    echo   "%ENGINE_DIR%\GenerateProjectFiles.bat"
    echo Please make sure UnrealEngine is at the same level as EagleWalk.
    pause
    exit /b 1
)

pushd "%ENGINE_DIR%"
call "%ENGINE_DIR%\GenerateProjectFiles.bat" -project="%PROJECT_UPROJECT%" -vscode
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

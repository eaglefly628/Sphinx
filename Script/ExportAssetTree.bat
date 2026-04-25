@echo off
REM =====================================================================
REM ExportAssetTree.bat
REM ---------------------------------------------------------------------
REM Export the directory structure + file sizes of a downloaded asset
REM pack into a single text file, so we can plan the SphinxFlight import
REM layout without uploading the whole thing.
REM
REM Usage:
REM   ExportAssetTree.bat                           (will prompt for path)
REM   ExportAssetTree.bat "C:\Downloads\F16_Pack"
REM
REM Output: asset_tree.txt next to this script
REM =====================================================================

setlocal enabledelayedexpansion
chcp 65001 >nul

REM --- Get target directory ---
set "TARGET=%~1"
if "%TARGET%"=="" (
    set /p TARGET="Drag asset folder here (or paste path) and press Enter: "
)

REM Strip surrounding quotes
set "TARGET=%TARGET:"=%"

if not exist "%TARGET%" (
    echo [ERROR] Folder not found: %TARGET%
    pause
    exit /b 1
)

set "OUTPUT=%~dp0asset_tree.txt"

echo.
echo Scanning: %TARGET%
echo Output:   %OUTPUT%
echo.

REM --- Header ---
> "%OUTPUT%" echo ============================================================
>> "%OUTPUT%" echo  ASSET PACK TREE EXPORT
>> "%OUTPUT%" echo  Source: %TARGET%
>> "%OUTPUT%" echo  Generated: %DATE% %TIME%
>> "%OUTPUT%" echo ============================================================
>> "%OUTPUT%" echo.

REM --- Section 1: ASCII tree (folders + files) ---
>> "%OUTPUT%" echo ----- DIRECTORY TREE -----
>> "%OUTPUT%" echo.
tree "%TARGET%" /F /A >> "%OUTPUT%"
>> "%OUTPUT%" echo.

REM --- Section 2: File summary by extension (PowerShell) ---
>> "%OUTPUT%" echo ----- FILE COUNT BY EXTENSION -----
>> "%OUTPUT%" echo.
powershell -NoProfile -Command ^
    "Get-ChildItem -Path '%TARGET%' -Recurse -File | ^
     Group-Object Extension | ^
     Sort-Object Count -Descending | ^
     ForEach-Object { ^
         $sizeMB = ($_.Group | Measure-Object Length -Sum).Sum / 1MB; ^
         '{0,-12} {1,6} files   {2,10:N2} MB' -f $_.Name, $_.Count, $sizeMB ^
     }" >> "%OUTPUT%"
>> "%OUTPUT%" echo.

REM --- Section 3: Top 30 largest files ---
>> "%OUTPUT%" echo ----- TOP 30 LARGEST FILES -----
>> "%OUTPUT%" echo.
powershell -NoProfile -Command ^
    "Get-ChildItem -Path '%TARGET%' -Recurse -File | ^
     Sort-Object Length -Descending | ^
     Select-Object -First 30 | ^
     ForEach-Object { ^
         $sizeMB = $_.Length / 1MB; ^
         $rel = $_.FullName.Substring($_.FullName.IndexOf('%TARGET:\=\\%') + 0); ^
         '{0,10:N2} MB   {1}' -f $sizeMB, $_.FullName ^
     }" >> "%OUTPUT%"
>> "%OUTPUT%" echo.

REM --- Section 4: Detect interesting asset categories ---
>> "%OUTPUT%" echo ----- UE5 ASSET CATEGORIES DETECTED -----
>> "%OUTPUT%" echo.
powershell -NoProfile -Command ^
    "$cats = @{ ^
        'SkeletalMesh' = '\\SK_|_Skeleton'; ^
        'AnimBP/Sequence' = '\\AnimBP|\\ABP_|_Anim|\\AS_'; ^
        'Material' = '\\M_|\\MI_|\\MM_'; ^
        'Texture' = '\\T_'; ^
        'Particle/Niagara' = '\\NS_|\\PS_|\\FX_|\\P_'; ^
        'Sound' = '\\.wav$|\\SC_|\\SW_'; ^
        'Blueprint' = '\\BP_'; ^
        'Mesh-Static' = '\\SM_'; ^
     }; ^
     foreach ($k in $cats.Keys) { ^
         $count = (Get-ChildItem -Path '%TARGET%' -Recurse -File | Where-Object { $_.FullName -match $cats[$k] }).Count; ^
         '{0,-22} {1} files' -f $k, $count ^
     }" >> "%OUTPUT%"
>> "%OUTPUT%" echo.

REM --- Section 5: Total size ---
>> "%OUTPUT%" echo ----- TOTAL SIZE -----
>> "%OUTPUT%" echo.
powershell -NoProfile -Command ^
    "$total = (Get-ChildItem -Path '%TARGET%' -Recurse -File | Measure-Object Length -Sum).Sum / 1MB; ^
     '{0:N2} MB ({1:N2} GB)' -f $total, ($total/1024)" >> "%OUTPUT%"

echo.
echo ============================================================
echo  DONE.
echo  Open: %OUTPUT%
echo  Then paste it back to me so I can plan the import layout.
echo ============================================================
echo.
pause

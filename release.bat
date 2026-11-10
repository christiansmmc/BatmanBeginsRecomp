@echo off
set "GAME_EXE=BatmanBegins"
cd /d "%~dp0"
set "OUT=out\build\release"
rem the Visual C++ runtime beside the executable, so a fresh Windows install needs nothing more
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -property installationPath`) do set "VSDIR=%%i"
for /d %%v in ("%VSDIR%\VC\Redist\MSVC\1*") do for /d %%c in ("%%v\x64\Microsoft.VC*.CRT") do set "VCRUNTIME=%%c"
if not exist "%VCRUNTIME%\vcruntime140.dll" (echo Visual C++ runtime not found: install Visual Studio with the C++ workload & exit /b 1)
if not exist "%OUT%\%GAME_EXE%.exe" call build.bat || exit /b 1
rem the configure step writes where the SDK is
if not exist "%OUT%\sdk_dir.txt" cmake --preset release >nul || exit /b 1
if not exist "%OUT%\sdk_dir.txt" echo %OUT%\sdk_dir.txt was not written, update the SDK. && exit /b 1
set /p SDK=<out\build\release\sdk_dir.txt
del /q "%GAME_EXE%.zip" 2>nul
rem an empty assets folder beside the exe, where the game (files or .iso) goes
rd /s /q "%OUT%\release" 2>nul
mkdir "%OUT%\release\assets"
echo Put the US ISO of Batman Begins for Xbox here, or the files extracted from it.> "%OUT%\release\assets\Put the game here.txt"
rem the player's readme, and the licences of this project and of the fonts inside the exe
copy /y docs\player-readme.txt "%OUT%\release\README.txt" >nul
rem the exe that ships is a copy with the project's own icon: the build's icon is made from the game's title image
copy /y "%OUT%\%GAME_EXE%.exe" "%OUT%\release\" >nul
python tools\release_icon.py "%OUT%\release\%GAME_EXE%.exe" || exit /b 1
rem only the runtime DLLs the executables import; xbe_recomp.exe needs its own copy, Windows only looks beside the
rem exe it starts
del /q "%OUT%\compiler\*.dll" 2>nul
for %%f in (msvcp140 vcruntime140) do copy /y "%VCRUNTIME%\%%f.dll" "%OUT%\compiler\" >nul
rem the compiler folder without its work folder: no game code goes in the zip, the first start builds it
7z a "%GAME_EXE%.zip" ".\%OUT%\release\%GAME_EXE%.exe" ".\%OUT%\release\assets" ".\%OUT%\compiler" ".\%OUT%\release\README.txt" .\LICENSE ".\%OUT%\OFL-Michroma.txt" ".\%OUT%\OFL-BarlowSemiCondensed.txt" "%SDK%\THIRD_PARTY.txt" "%VCRUNTIME%\msvcp140.dll" "%VCRUNTIME%\vcruntime140.dll" "%VCRUNTIME%\vcruntime140_1.dll" -xr!work

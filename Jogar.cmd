@echo off
cd /d "%~dp0"
if not exist "SuperMonacoDuoRecomp.exe" (
 echo Run the installer from the launcher or scripts\build.ps1 to prepare the games.
 pause
 exit /b 1
)
start "" "%~dp0SuperMonacoDuoRecomp.exe"

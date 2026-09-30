@echo off
rem POLOOYX installer: copies the VST3 into the standard Windows VST3 folder.
rem Needs admin rights for "C:\Program Files\Common Files\VST3" - it will ask.

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo Requesting administrator permission...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)

set "SRC=%~dp0POLOOYX.vst3"
set "DST=%CommonProgramFiles%\VST3\POLOOYX.vst3"

if not exist "%SRC%" (
    echo Could not find POLOOYX.vst3 next to this installer. Unzip the whole download first.
    pause
    exit /b 1
)

echo Installing POLOOYX to "%DST%" ...
if exist "%DST%" rmdir /S /Q "%DST%"
xcopy "%SRC%" "%DST%\" /E /I /Y /Q >nul
if %errorlevel% neq 0 (
    echo Install failed. Close FL Studio and try again.
    pause
    exit /b 1
)

echo.
echo Done. In FL Studio: Options ^> Manage plugins ^> Find installed plugins,
echo then look for POLOOYX under Effects (vendor: Polooyx).
echo.
pause

@echo off
rem ZaY-o-Nator by BeatsbyLonza: puts the VST3 in Windows' shared VST3 folder.
setlocal
net session >nul 2>&1
if not "%errorlevel%"=="0" (
  echo Asking Windows for permission to install into Program Files...
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
if exist "%DEST%\ZaY-o-Nator.vst3" rmdir /s /q "%DEST%\ZaY-o-Nator.vst3"
xcopy /e /i /y /q "%~dp0ZaY-o-Nator.vst3" "%DEST%\ZaY-o-Nator.vst3" >nul
if exist "%DEST%\ZaY-o-Nator.vst3" (
  echo.
  echo   ZaY-o-Nator is installed in %DEST%
  echo   Restart your DAW or rescan plugins. It shows up under BeatsbyLonza.
) else (
  echo.
  echo   Could not copy the plugin. Copy the ZaY-o-Nator.vst3 folder into %DEST% yourself.
)
echo.
pause

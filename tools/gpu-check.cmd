@echo off
rem Drag a video file onto this file. It asks rubraview what this PC can do
rem with decoding on the graphics card (T065, RV-062) and writes the answer
rem to gpu-check.txt next to it - send that file back.
setlocal
if "%~1"=="" (
  echo Drag a video file onto gpu-check.cmd.
  pause
  exit /b 1
)
set "EXE="
for %%F in ("%~dp0rubraview-v*.exe") do set "EXE=%%F"
if not defined EXE (
  echo rubraview-v*.exe is not in this folder.
  pause
  exit /b 1
)
"%EXE%" --probe-gpu "%~1" > "%~dp0gpu-check.txt" 2>&1
echo.>> "%~dp0gpu-check.txt"
echo file: %~nx1>> "%~dp0gpu-check.txt"
type "%~dp0gpu-check.txt"
echo.
echo Saved to %~dp0gpu-check.txt
pause

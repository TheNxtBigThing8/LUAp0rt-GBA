@echo off
rem LUAp0rt Launcher -- double-click to open the dashboard.
rem Wraps the frozen tools/send.py and tools/upload.py; edits nothing.
cd /d "%~dp0"
where python >nul 2>nul
if errorlevel 1 (
  echo Python 3 was not found on PATH. Install it from python.org and try again.
  pause
  exit /b 1
)
python luap0rt_launcher.py %*
if errorlevel 1 pause

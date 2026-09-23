@echo off
rem Build LUAp0rt-Launcher.exe with PyInstaller.
rem
rem   launcher\build_exe.cmd
rem
rem Output: launcher\LUAp0rt-Launcher.exe + launcher\_internal\  (folder build, no console window)
rem
rem Folder build on purpose: a one-file exe unpacks itself on EVERY start, and the
rem launcher starts one child process per file it sends (--run-tool), which cost
rem ~1.5 s per file; the folder build starts in ~0.2 s. Keep _internal next to the exe.
rem
rem The exe bundles a Python interpreter and the ui\ folder. It must stay in
rem the launcher\ folder: at run time it locates tools\send.py, tools\upload.py
rem and the payload pairs relative to its own location, exactly like the .py.
rem The frozen tools themselves are NOT bundled; they are executed from the
rem project tree, unmodified, via the exe's --run-tool mode.
rem
rem Requires: a Python 3 with PyInstaller, pystray and Pillow installed:
rem     py -3.11 -m pip install pyinstaller pystray pillow
rem The Microsoft Store Python cannot build exes; use a python.org build.

setlocal
cd /d "%~dp0"

set PY=
for %%V in (3.12 3.11 3.13 3.14) do (
  if not defined PY (
    py -%%V -c "import PyInstaller" >nul 2>nul && set PY=py -%%V
  )
)
if not defined PY (
  python -c "import PyInstaller" >nul 2>nul && set PY=python
)
if not defined PY (
  echo No Python with PyInstaller found. Install it with:
  echo     py -3.11 -m pip install pyinstaller
  exit /b 1
)

echo Building with: %PY%
%PY% -m PyInstaller --noconfirm --clean ^
  --onedir --windowed ^
  --name LUAp0rt-Launcher ^
  --icon "%~dp0ui\mascot.ico" ^
  --add-data "%~dp0ui;ui" ^
  --hidden-import pystray._win32 ^
  --hidden-import PIL.Image ^
  --hidden-import PIL.ImageDraw ^
  --distpath build_tmp\dist ^
  --workpath build_tmp\work ^
  --specpath build_tmp ^
  luap0rt_launcher.py
if errorlevel 1 exit /b 1

rem move the folder build into place: the exe next to this script, _internal beside it
if exist _internal rmdir /s /q _internal
move /y build_tmp\dist\LUAp0rt-Launcher\LUAp0rt-Launcher.exe LUAp0rt-Launcher.exe >nul
move /y build_tmp\dist\LUAp0rt-Launcher\_internal _internal >nul
rmdir /s /q build_tmp >nul 2>nul
echo.
echo Built: %~dp0LUAp0rt-Launcher.exe
endlocal

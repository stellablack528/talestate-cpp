@echo off
cd /d "%~dp0"
if exist ".tools\renpy-8.6.0-sdk\renpy.py" (
  ".tools\renpy-8.6.0-sdk\lib\py3-windows-x86_64\python.exe" scripts\run_game.py
  exit /b
)
start "" "dist\TaleState-0.3.0-pc\TaleState.exe"

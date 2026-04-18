@echo off
setlocal

set "ROOT_DIR=%~dp0"
set "APP_DIR=%ROOT_DIR%companion-web"

if not exist "%APP_DIR%\package.json" (
  echo [ERROR] companion-web\package.json not found.
  echo Make sure this file is in the project root folder.
  pause
  exit /b 1
)

where npm >nul 2>nul
if errorlevel 1 (
  echo [ERROR] npm was not found in PATH.
  echo Install Node.js LTS and reopen this script.
  pause
  exit /b 1
)

cd /d "%APP_DIR%"

if not exist "node_modules" (
  echo Installing dependencies...
  call npm install
  if errorlevel 1 (
    echo [ERROR] npm install failed.
    pause
    exit /b 1
  )
)

echo Starting companion app...
start "" "http://127.0.0.1:5173"
call npm run dev

endlocal

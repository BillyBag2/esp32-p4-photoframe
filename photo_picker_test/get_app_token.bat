@echo off
if not exist "%~dp0client_secret_desktop.json" (
  echo Place a Google OAuth Desktop app client JSON at:
  echo   %~dp0client_secret_desktop.json
  exit /b 1
)
python "%~dp0photo_picker.py" ^
  --client-secrets "%~dp0client_secret_desktop.json" ^
  --auth-flow desktop ^
  --token-file "%~dp0token_picker_app.json"

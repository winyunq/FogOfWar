@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Enable-UE58BuiltInMCP.ps1" %*
if errorlevel 1 (
  echo.
  echo Failed to enable UE 5.8 MCP.
  pause
  exit /b 1
)
echo.
echo UE 5.8 MCP is enabled. Restart Unreal Editor to load the server and toolsets.
pause

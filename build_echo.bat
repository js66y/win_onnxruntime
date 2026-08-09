@echo off
setlocal EnableExtensions
cd /d "%~dp0"

set "VCVARS=D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
set "CMAKE=D:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"

if not exist "%VCVARS%" (
  echo [ERROR] vcvars64.bat not found:
  echo   %VCVARS%
  exit /b 1
)
if not exist "%CMAKE%" (
  echo [ERROR] cmake.exe not found:
  echo   %CMAKE%
  exit /b 1
)

echo [1/4] Init x64 MSVC environment
call "%VCVARS%"
if errorlevel 1 exit /b 1

rem 传入 clean 参数时才全量重建: build_echo.bat clean
if /i "%~1"=="clean" (
  echo [2/4] Clean build/
  if exist build rmdir /s /q build
) else (
  echo [2/4] Incremental build ^(pass "clean" to wipe build/^)
)

echo [3/4] Configure
"%CMAKE%" -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 exit /b 1

echo [4/4] Build Release
"%CMAKE%" --build build --config Release
if errorlevel 1 exit /b 1

echo.
echo Build OK: build\bin\Release\echo.exe
echo To run: build\bin\Release\echo.exe
endlocal

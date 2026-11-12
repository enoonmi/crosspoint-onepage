@echo off
REM Host-side test runner for AnxSync pure-logic modules (Windows / MSVC).
REM
REM These modules are written without Arduino / ESP-IDF dependencies so the
REM parsing and decision logic can be verified on a development machine instead
REM of by flashing hardware.
REM
REM Prefer run_tests.ps1: this .cmd exists for callers that cannot change the
REM PowerShell execution policy.

setlocal enabledelayedexpansion

set "HERE=%~dp0"
set "ROOT=%~dp0..\.."
set "LIBDIR=%ROOT%\lib\AnxSync"
set "BUILD=%HERE%build"

if not exist "%BUILD%" mkdir "%BUILD%"

set "VCVARS="
for %%d in (
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
) do (
  if exist %%d set "VCVARS=%%~d"
)

if "%VCVARS%"=="" (
  echo ERROR: vcvars64.bat not found. Install VS Build Tools with the C++ workload.
  exit /b 1
)

set "TMP=%BUILD%"
set "TEMP=%BUILD%"

set "SOURCES="
for %%f in ("%LIBDIR%\*.cpp") do set "SOURCES=!SOURCES! "%%f""
for %%f in ("%HERE%test_*.cpp") do set "SOURCES=!SOURCES! "%%f""

if "!SOURCES!"=="" (
  echo ERROR: no sources found.
  exit /b 1
)

call "%VCVARS%" >nul 2>&1
if errorlevel 1 (
  echo ERROR: failed to initialise the MSVC environment.
  exit /b 1
)

cd /d "%BUILD%"

cl /nologo /utf-8 /std:c++20 /EHsc /W4 /I"%LIBDIR%" /Fo:"%BUILD%\\" /Fe:"%BUILD%\anx_tests.exe" !SOURCES!
if errorlevel 1 (
  echo.
  echo COMPILATION FAILED
  exit /b 1
)

echo.
"%BUILD%\anx_tests.exe"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo TESTS PASSED) else (echo TESTS FAILED ^(exit %RC%^))
exit /b %RC%

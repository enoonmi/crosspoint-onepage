# Host-side test runner for AnxSync pure-logic modules.
#
# These modules are written without Arduino / ESP-IDF dependencies on purpose,
# so the parsing and decision logic can be verified on a development machine
# instead of by flashing hardware.
#
# Usage, from the firmware/ directory:
#     powershell -ExecutionPolicy Bypass -File tests\host\run_tests.ps1
#
# Notes for this machine:
#   * MSVC (VS 2022 Build Tools) is used because no gcc/clang is installed.
#   * TMP/TEMP are redirected into tests/host/build: under some sandboxes the
#     default %TEMP% subdirectory may not exist, and the MSVC linker then fails
#     with LNK1104 while writing its own scratch file.
#   * The .bat is generated here rather than hand-written because quoting
#     "Program Files" through nested shells is fragile.

param(
  [switch]$KeepBuild
)

$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$firmwareRoot = Split-Path -Parent (Split-Path -Parent $here)
$libDir = Join-Path $firmwareRoot 'lib\AnxSync'
$buildDir = Join-Path $here 'build'

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) {
  throw "vcvars64.bat not found. Install VS Build Tools with the C++ workload."
}

$libSources = @(Get-ChildItem -Path $libDir -Filter '*.cpp' -File | ForEach-Object { '"' + $_.FullName + '"' })
$testSources = @(Get-ChildItem -Path $here -Filter 'test_*.cpp' -File | ForEach-Object { '"' + $_.FullName + '"' })

if ($testSources.Count -eq 0) { throw "No test_*.cpp found in $here" }
if ($libSources.Count -eq 0) { throw "No .cpp found in $libDir" }

$allSources = ($libSources + $testSources) -join ' '

$batPath = Join-Path $buildDir 'run_tests_build.bat'
$batLines = @(
  '@echo off'
  "set TMP=$buildDir"
  "set TEMP=$buildDir"
  "call `"$vcvars`" >nul 2>&1"
  'if errorlevel 1 ( echo ERROR: vcvars failed & exit /b 1 )'
  "cd /d `"$buildDir`""
  "cl /nologo /utf-8 /std:c++20 /EHsc /W4 /I`"$libDir`" /Fo:`"$buildDir\\`" /Fe:`"$buildDir\anx_tests.exe`" $allSources"
  'if errorlevel 1 ( echo COMPILATION_FAILED & exit /b 1 )'
  'echo.'
  "`"$buildDir\anx_tests.exe`""
  'exit /b %ERRORLEVEL%'
)
Set-Content -LiteralPath $batPath -Value $batLines -Encoding ascii

& cmd /c $batPath
$rc = $LASTEXITCODE

if (-not $KeepBuild) {
  Remove-Item -Path (Join-Path $buildDir '*.obj') -Force -ErrorAction SilentlyContinue
}

if ($rc -ne 0) {
  throw "AnxSync host tests failed (exit $rc)."
}
Write-Host "OK: AnxSync host tests passed." -ForegroundColor Green

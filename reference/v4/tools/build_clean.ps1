$ErrorActionPreference = "Stop"

$idfRoot = "C:\Users\doeki\esp\v5.5.1\esp-idf"
$idfPython = "C:\Users\doeki\.espressif\python_env\idf5.5_py3.11_env\Scripts"
$env:Path = "$idfPython;$env:Path"
. "$idfRoot\export.ps1"

# Codex/CI shells can filter PATH changes made by export.ps1. Keep the clean
# build reproducible by naming the ESP-IDF-managed host tools explicitly.
$idfNinja = "C:\Users\doeki\.espressif\tools\ninja\1.12.1"
$idfCmake = "C:\Users\doeki\.espressif\tools\cmake\3.30.2\bin"
$idfCcache = "C:\Users\doeki\.espressif\tools\ccache\4.11.2\ccache-4.11.2-windows-x86_64"
$idfToolchain = "C:\Users\doeki\.espressif\tools\xtensa-esp-elf\esp-14.2.0_20241119\xtensa-esp-elf\bin"
$env:Path = "$idfNinja;$idfCmake;$idfCcache;$idfToolchain;$env:Path"
$ninjaExe = Join-Path $idfNinja "ninja.exe"
$ccacheExe = Join-Path $idfCcache "ccache.exe"
$cCompiler = Join-Path $idfToolchain "xtensa-esp32s3-elf-gcc.exe"
$cxxCompiler = Join-Path $idfToolchain "xtensa-esp32s3-elf-g++.exe"
if (!(Test-Path -LiteralPath $ninjaExe)) { throw "ninja.exe not found: $ninjaExe" }
if (!(Test-Path -LiteralPath $ccacheExe)) { throw "ccache.exe not found: $ccacheExe" }
if (!(Test-Path -LiteralPath $cCompiler)) { throw "C compiler not found: $cCompiler" }
if (!(Test-Path -LiteralPath $cxxCompiler)) { throw "C++ compiler not found: $cxxCompiler" }

$buildDirectory = "build-v4-7-clean"
if (Test-Path "$buildDirectory\CMakeCache.txt") {
    idf.py -B $buildDirectory fullclean
    if ($LASTEXITCODE -ne 0) { throw "idf.py fullclean failed: $LASTEXITCODE" }
}

idf.py -B $buildDirectory -D IDF_TARGET=esp32s3 `
    -D CMAKE_MAKE_PROGRAM=$ninjaExe `
    -D CMAKE_C_COMPILER=$cCompiler `
    -D CMAKE_CXX_COMPILER=$cxxCompiler `
    -D CMAKE_ASM_COMPILER=$cCompiler `
    -D CMAKE_C_COMPILER_LAUNCHER=$ccacheExe `
    -D CMAKE_CXX_COMPILER_LAUNCHER=$ccacheExe build
if ($LASTEXITCODE -ne 0) { throw "idf.py build failed: $LASTEXITCODE" }

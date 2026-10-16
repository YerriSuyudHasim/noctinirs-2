param(
    [string]$BuildDirectory = "build-v4-7-2-idf-5-5-5"
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
    throw "IDF_PATH is not set. Open an ESP-IDF v5.5.5 PowerShell first."
}

$idfPy = Get-Command idf.py -ErrorAction SilentlyContinue
if ($null -eq $idfPy) {
    throw "idf.py is not available. Activate the ESP-IDF v5.5.5 environment first."
}

$idfVersion = (& idf.py --version 2>&1 | Out-String).Trim()
if ($LASTEXITCODE -ne 0) {
    throw "Unable to query ESP-IDF version."
}
if ($idfVersion -notmatch 'v?5\.5\.5(?:\D|$)') {
    throw "This patch requires ESP-IDF v5.5.5 exactly; active version: $idfVersion"
}

$resolvedProject = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$resolvedBuild = Join-Path $resolvedProject $BuildDirectory

Push-Location $resolvedProject
try {
    if (Test-Path -LiteralPath (Join-Path $resolvedBuild 'CMakeCache.txt')) {
        & idf.py -B $resolvedBuild fullclean
        if ($LASTEXITCODE -ne 0) {
            throw "idf.py fullclean failed: $LASTEXITCODE"
        }
    }

    & idf.py -B $resolvedBuild -D IDF_TARGET=esp32s3 build
    if ($LASTEXITCODE -ne 0) {
        throw "idf.py build failed: $LASTEXITCODE"
    }
} finally {
    Pop-Location
}

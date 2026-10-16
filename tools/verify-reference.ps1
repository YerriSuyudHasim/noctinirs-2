[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$referenceRoot = Join-Path $repoRoot 'reference'
$manifestPath = Join-Path $referenceRoot 'MANIFEST.sha256'

if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    throw "Reference manifest not found: $manifestPath"
}

$expected = @{}
foreach ($line in Get-Content -LiteralPath $manifestPath) {
    if ([string]::IsNullOrWhiteSpace($line)) {
        continue
    }

    if ($line -notmatch '^([0-9a-f]{64})  (.+)$') {
        throw "Invalid manifest line: $line"
    }

    $expected[$Matches[2]] = $Matches[1]
}

$actual = @{}
Get-ChildItem -LiteralPath $referenceRoot -File -Recurse -Force |
    Where-Object { $_.FullName -ne $manifestPath } |
    ForEach-Object {
        $relative = [IO.Path]::GetRelativePath($referenceRoot, $_.FullName).Replace('\', '/')
        $actual[$relative] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }

$problems = [System.Collections.Generic.List[string]]::new()

foreach ($path in $expected.Keys) {
    if (-not $actual.ContainsKey($path)) {
        $problems.Add("Missing: $path")
    }
    elseif ($actual[$path] -ne $expected[$path]) {
        $problems.Add("Modified: $path")
    }
}

foreach ($path in $actual.Keys) {
    if (-not $expected.ContainsKey($path)) {
        $problems.Add("Unexpected: $path")
    }
}

if ($problems.Count -gt 0) {
    $problems | Sort-Object | ForEach-Object { Write-Error $_ }
    throw "Reference integrity verification failed with $($problems.Count) problem(s)."
}

Write-Host "Reference integrity verified: $($actual.Count) files."

param(
    [string]$BuildDir = "build/windows-release",
    [string]$OutputRoot = "dist",
    [string]$PackageName = "",
    [string]$DesktopZipPath = "",
    [switch]$NoZip
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$PathValue"))
}

function Copy-FileRequired([string]$Source, [string]$DestinationDir) {
    if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
        throw "Missing required file: $Source"
    }
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    Copy-Item -LiteralPath $Source -Destination (Join-Path $DestinationDir (Split-Path $Source -Leaf)) -Force
}

function Copy-TreeFiltered([string]$SourceDir, [string]$DestinationDir, [string[]]$ExcludeFragments) {
    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
        throw "Missing required directory: $SourceDir"
    }
    $sourceFull = [System.IO.Path]::GetFullPath($SourceDir)
    $sourcePrefix = $sourceFull.TrimEnd('\') + '\'
    Get-ChildItem -LiteralPath $sourceFull -Recurse -File | ForEach-Object {
        $fileFull = [System.IO.Path]::GetFullPath($_.FullName)
        foreach ($fragment in $ExcludeFragments) {
            if ($fileFull.Contains($fragment)) {
                return
            }
        }
        if (-not $fileFull.StartsWith($sourcePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected file outside source tree: $fileFull"
        }
        $relative = $fileFull.Substring($sourcePrefix.Length)
        $target = Join-Path $DestinationDir $relative
        New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
        Copy-Item -LiteralPath $fileFull -Destination $target -Force
    }
}

function Find-LatestVcRuntimeDir {
    $redistRoot = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Redist\MSVC"
    if (-not (Test-Path -LiteralPath $redistRoot -PathType Container)) {
        return $null
    }
    $candidates = Get-ChildItem -LiteralPath $redistRoot -Directory |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
        Where-Object { Test-Path -LiteralPath $_ -PathType Container }
    return $candidates | Select-Object -First 1
}

$buildPath = Resolve-RepoPath $BuildDir
$outputRootPath = Resolve-RepoPath $OutputRoot
if (-not (Test-Path -LiteralPath $buildPath -PathType Container)) {
    throw "Build directory not found: $buildPath"
}

if ([string]::IsNullOrWhiteSpace($PackageName)) {
    $PackageName = "AdayoCorpusTool-win-x64-" + (Get-Date -Format "yyyyMMdd-HHmmss")
}

$packageDir = Join-Path $outputRootPath $PackageName
if (Test-Path -LiteralPath $packageDir) {
    throw "Package directory already exists, refusing to overwrite: $packageDir"
}

New-Item -ItemType Directory -Force -Path $packageDir | Out-Null

Copy-FileRequired (Join-Path $buildPath "AdayoCorpusTool.exe") $packageDir
Get-ChildItem -LiteralPath $buildPath -Filter "*.dll" -File | ForEach-Object {
    Copy-FileRequired $_.FullName $packageDir
}

$vcRuntimeDir = Find-LatestVcRuntimeDir
if ($vcRuntimeDir) {
    foreach ($name in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll")) {
        $candidate = Join-Path $vcRuntimeDir $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            Copy-FileRequired $candidate $packageDir
        }
    }
}

foreach ($runtimeDir in @("logs", "cache", "exports", "config")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $packageDir $runtimeDir) | Out-Null
}

$modelsDest = Join-Path $packageDir "models"
New-Item -ItemType Directory -Force -Path $modelsDest | Out-Null
Copy-FileRequired (Resolve-RepoPath "models/README.md") $modelsDest

$sherpaDest = Join-Path $modelsDest "sherpa"
foreach ($voiceDir in @("vits-piper-en_US-amy-low", "vits-piper-zh_CN-xiao_ya-medium-int8")) {
    $source = Resolve-RepoPath ("models/sherpa/" + $voiceDir)
    $destination = Join-Path $sherpaDest $voiceDir
    Copy-TreeFiltered $source $destination @("\.cache\", "\_download_test\")
}

$docsDest = Join-Path $packageDir "docs"
New-Item -ItemType Directory -Force -Path $docsDest | Out-Null
foreach ($docName in @("PROJECT_STATUS.md", "EXECUTION_NOTES.md", "dependency-lock.md", "CODEX_EXECUTION_PLAN.md")) {
    Copy-FileRequired (Resolve-RepoPath ("docs/" + $docName)) $docsDest
}
Copy-FileRequired (Resolve-RepoPath "README.md") $packageDir

$auditDest = Join-Path $docsDest "audit"
Copy-TreeFiltered (Resolve-RepoPath "docs/audit") $auditDest @()

$manifestPath = Join-Path $packageDir "PACKAGE_MANIFEST.txt"
$files = Get-ChildItem -LiteralPath $packageDir -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $packagePrefix = ([System.IO.Path]::GetFullPath($packageDir)).TrimEnd('\') + '\'
        $fileFull = [System.IO.Path]::GetFullPath($_.FullName)
        if (-not $fileFull.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected file outside package tree: $fileFull"
        }
        $relative = $fileFull.Substring($packagePrefix.Length)
        "$relative`t$($_.Length)"
    }
Set-Content -LiteralPath $manifestPath -Encoding UTF8 -Value @(
    "Package: $PackageName",
    "Created: $(Get-Date -Format o)",
    "Source commit: $(git -C (Resolve-RepoPath '.') rev-parse --short HEAD)",
    "",
    "Files:",
    $files
)

$zipPath = $null
if (-not $NoZip) {
    if ([string]::IsNullOrWhiteSpace($DesktopZipPath)) {
        $desktop = Join-Path $env:USERPROFILE "Desktop"
        $DesktopZipPath = Join-Path $desktop ($PackageName + ".zip")
    }
    if (Test-Path -LiteralPath $DesktopZipPath) {
        throw "ZIP already exists, refusing to overwrite: $DesktopZipPath"
    }
    Compress-Archive -LiteralPath $packageDir -DestinationPath $DesktopZipPath -CompressionLevel Optimal
    $zipPath = [System.IO.Path]::GetFullPath($DesktopZipPath)
}

[pscustomobject]@{
    PackageDir = [System.IO.Path]::GetFullPath($packageDir)
    ZipPath = $zipPath
    FileCount = (Get-ChildItem -LiteralPath $packageDir -Recurse -File | Measure-Object).Count
    ByteCount = (Get-ChildItem -LiteralPath $packageDir -Recurse -File | Measure-Object Length -Sum).Sum
}

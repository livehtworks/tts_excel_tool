param(
    [string]$BuildDir = "build/windows-release",
    [string]$OutputRoot = "dist",
    [string]$PackageName = "AdayoCorpusTool",
    [string]$ModelSourceRoot = "dist/AdayoCorpusTool/model",
    [string]$ZipPath = "",
    [switch]$Zip
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

function Copy-TreeRequired([string]$SourceDir, [string]$DestinationDir) {
    if (-not (Test-Path -LiteralPath $SourceDir -PathType Container)) {
        throw "Missing required directory: $SourceDir"
    }
    New-Item -ItemType Directory -Force -Path $DestinationDir | Out-Null
    Copy-Item -LiteralPath (Join-Path $SourceDir "*") -Destination $DestinationDir -Recurse -Force
}

function Assert-ContainedRelativePath([string]$Root, [string]$Value, [string]$Label, [string]$ModelId, [switch]$Directory) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        if ($Label -in @("model", "tokens")) {
            throw "Model '$ModelId' missing required '$Label' in model.json"
        }
        return
    }
    if ([System.IO.Path]::IsPathRooted($Value)) {
        throw "Model '$ModelId' $Label must be relative, not absolute: $Value"
    }
    $rootFull = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $candidate = [System.IO.Path]::GetFullPath((Join-Path $rootFull $Value))
    $prefix = $rootFull + '\'
    if (($candidate -ne $rootFull) -and (-not $candidate.StartsWith($prefix, [System.StringComparison]::OrdinalIgnoreCase))) {
        throw "Model '$ModelId' $Label escapes model root: $Value"
    }
    if ($Directory) {
        if (-not (Test-Path -LiteralPath $candidate -PathType Container)) {
            throw "Model '$ModelId' missing $Label directory: $candidate"
        }
    } else {
        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
            throw "Model '$ModelId' missing $Label file: $candidate"
        }
    }
}

function Test-SherpaModelDirectory([string]$ModelRoot, [string]$ExpectedId) {
    if (-not (Test-Path -LiteralPath $ModelRoot -PathType Container)) {
        throw "Missing model directory for manifest id '$ExpectedId': $ModelRoot"
    }
    $modelJsonPath = Join-Path $ModelRoot "model.json"
    if (-not (Test-Path -LiteralPath $modelJsonPath -PathType Leaf)) {
        throw "Missing model.json for manifest id '$ExpectedId': $modelJsonPath"
    }
    $modelJson = Get-Content -LiteralPath $modelJsonPath -Encoding UTF8 -Raw | ConvertFrom-Json
    if ([string]$modelJson.id -ne $ExpectedId) {
        throw "Model manifest id '$ExpectedId' does not match model.json id '$($modelJson.id)'"
    }
    if ([string]$modelJson.engine_id -ne "sherpa-vits") {
        throw "Model '$ExpectedId' has unsupported engine_id '$($modelJson.engine_id)'"
    }
    if ([string]::IsNullOrWhiteSpace([string]$modelJson.language_code)) {
        throw "Model '$ExpectedId' missing language_code"
    }
    Assert-ContainedRelativePath $ModelRoot ([string]$modelJson.model) "model" $ExpectedId
    Assert-ContainedRelativePath $ModelRoot ([string]$modelJson.tokens) "tokens" $ExpectedId
    Assert-ContainedRelativePath $ModelRoot ([string]$modelJson.data_dir) "data_dir" $ExpectedId -Directory
    Assert-ContainedRelativePath $ModelRoot ([string]$modelJson.lexicon) "lexicon" $ExpectedId
    $ruleFsts = [string]$modelJson.rule_fsts
    if (-not [string]::IsNullOrWhiteSpace($ruleFsts)) {
        foreach ($item in $ruleFsts.Split(",")) {
            Assert-ContainedRelativePath $ModelRoot $item.Trim() "rule_fsts" $ExpectedId
        }
    }
}

function Find-VcRuntimeDir {
    if ($env:VCToolsRedistDir) {
        $candidate = Join-Path $env:VCToolsRedistDir "x64\Microsoft.VC143.CRT"
        if (Test-Path -LiteralPath $candidate -PathType Container) { return $candidate }
    }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $install = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $redist = Join-Path $install "VC\Redist\MSVC"
            if (Test-Path -LiteralPath $redist -PathType Container) {
                $candidate = Get-ChildItem -LiteralPath $redist -Directory |
                    Sort-Object Name -Descending |
                    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } |
                    Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
                    Select-Object -First 1
                if ($candidate) { return $candidate }
            }
        }
    }
    throw "Unable to locate VC runtime redist. Set VCToolsRedistDir or install VS Build Tools with VC redist."
}

function Find-Dumpbin {
    $command = Get-Command dumpbin.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $install = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($install) {
            $candidate = Get-ChildItem -LiteralPath (Join-Path $install "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe -File |
                Where-Object { $_.FullName -like "*Hostx64*x64*" } |
                Sort-Object FullName -Descending |
                Select-Object -First 1
            if ($candidate) { return $candidate.FullName }
        }
    }
    throw "Unable to locate dumpbin.exe for dependency closure validation."
}

function Get-Dependents([string]$Dumpbin, [string]$Binary) {
    $lines = & $Dumpbin /dependents $Binary 2>$null
    $lines |
        Where-Object { $_ -match '^\s+[A-Za-z0-9_.+-]+\.dll\s*$' } |
        ForEach-Object { $_.Trim().ToLowerInvariant() } |
        Sort-Object -Unique
}

function Test-DependencyClosure([string]$PackageDir) {
    $dumpbin = Find-Dumpbin
    $systemDlls = @(
        "advapi32.dll","bcrypt.dll","cfgmgr32.dll","comctl32.dll","comdlg32.dll","crypt32.dll","dwmapi.dll",
        "gdi32.dll","gdiplus.dll","imm32.dll","kernel32.dll","msimg32.dll","msvcrt.dll","ole32.dll",
        "oleacc.dll","oleaut32.dll","rpcrt4.dll","sechost.dll","setupapi.dll","shell32.dll","shcore.dll",
        "shlwapi.dll","ucrtbase.dll","user32.dll","uxtheme.dll","version.dll","wininet.dll","winmm.dll",
        "winspool.drv","ws2_32.dll","wsock32.dll"
    )
    $allow = @{}
    foreach ($name in $systemDlls) { $allow[$name] = $true }
    $known = @{}
    Get-ChildItem -LiteralPath $PackageDir -File | Where-Object {
        $_.Extension -in @(".exe", ".dll")
    } | ForEach-Object {
        $known[$_.Name.ToLowerInvariant()] = $_.FullName
    }
    $queue = New-Object System.Collections.Generic.Queue[string]
    $queue.Enqueue((Join-Path $PackageDir "AdayoCorpusTool.exe"))
    $seen = @{}
    while ($queue.Count -gt 0) {
        $binary = $queue.Dequeue()
        $key = [System.IO.Path]::GetFullPath($binary).ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        foreach ($dep in Get-Dependents $dumpbin $binary) {
            if ($allow.ContainsKey($dep)) { continue }
            if (-not $known.ContainsKey($dep)) {
                throw "Unresolved non-system DLL dependency '$dep' required by $binary"
            }
            $queue.Enqueue($known[$dep])
        }
    }
}

$buildPath = Resolve-RepoPath $BuildDir
$outputRootPath = Resolve-RepoPath $OutputRoot
$modelSourcePath = Resolve-RepoPath $ModelSourceRoot
$packageManifestSource = Resolve-RepoPath "models/package-manifest.json"
if (-not (Test-Path -LiteralPath $buildPath -PathType Container)) { throw "Build directory not found: $buildPath" }
if (-not (Test-Path -LiteralPath $modelSourcePath -PathType Container)) { throw "ModelSourceRoot not found: $modelSourcePath" }

$buildId = Get-Date -Format "yyyyMMdd-HHmmss"
$stagingRoot = Join-Path $outputRootPath ".staging\$PackageName-$buildId"
$stagingPackageDir = Join-Path $stagingRoot $PackageName
$finalPackageDir = Join-Path $outputRootPath $PackageName
if (Test-Path -LiteralPath $stagingRoot) { Remove-Item -LiteralPath $stagingRoot -Recurse -Force }
New-Item -ItemType Directory -Force -Path $stagingPackageDir | Out-Null

Copy-FileRequired (Join-Path $buildPath "AdayoCorpusTool.exe") $stagingPackageDir
Get-ChildItem -LiteralPath $buildPath -Filter "*.dll" -File | ForEach-Object {
    Copy-FileRequired $_.FullName $stagingPackageDir
}

$vcRuntimeDir = Find-VcRuntimeDir
foreach ($name in @("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll", "concrt140.dll")) {
    $candidate = Join-Path $vcRuntimeDir $name
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Missing required VC runtime DLL: $candidate"
    }
    Copy-FileRequired $candidate $stagingPackageDir
}

foreach ($runtimeDir in @("logs", "cache", "exports", "config")) {
    New-Item -ItemType Directory -Force -Path (Join-Path $stagingPackageDir $runtimeDir) | Out-Null
}

$modelsDest = Join-Path $stagingPackageDir "model"
New-Item -ItemType Directory -Force -Path $modelsDest | Out-Null
Copy-FileRequired (Resolve-RepoPath "models/README.md") $modelsDest
Copy-FileRequired $packageManifestSource $modelsDest
$packageManifest = Get-Content -LiteralPath $packageManifestSource -Encoding UTF8 -Raw | ConvertFrom-Json
if ($packageManifest.schema_version -ne 1) { throw "Unsupported model package manifest schema_version: $($packageManifest.schema_version)" }
if (-not $packageManifest.models -or $packageManifest.models.Count -eq 0) { throw "Model package manifest has no models" }

foreach ($model in $packageManifest.models) {
    $modelId = [string]$model.id
    if ([string]::IsNullOrWhiteSpace($modelId)) { throw "Model package manifest contains an entry without id" }
    $source = Join-Path (Join-Path $modelSourcePath "sherpa") $modelId
    Test-SherpaModelDirectory $source $modelId
    $target = Join-Path (Join-Path $modelsDest "sherpa") $modelId
    Copy-TreeRequired $source $target
    Test-SherpaModelDirectory $target $modelId
}

$docsDest = Join-Path $stagingPackageDir "docs"
New-Item -ItemType Directory -Force -Path $docsDest | Out-Null
foreach ($docName in @("PROJECT_STATUS.md", "EXECUTION_NOTES.md", "dependency-lock.md", "ARCHITECTURE.md", "BUSINESS_BASELINE.md", "VERIFICATION_REPORT.md")) {
    Copy-FileRequired (Resolve-RepoPath ("docs/" + $docName)) $docsDest
}
Copy-FileRequired (Resolve-RepoPath "README.md") $stagingPackageDir

Test-DependencyClosure $stagingPackageDir

$manifestPath = Join-Path $stagingPackageDir "PACKAGE_MANIFEST.txt"
$packagePrefix = ([System.IO.Path]::GetFullPath($stagingPackageDir)).TrimEnd('\') + '\'
$files = Get-ChildItem -LiteralPath $stagingPackageDir -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $fileFull = [System.IO.Path]::GetFullPath($_.FullName)
        if (-not $fileFull.StartsWith($packagePrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Unexpected file outside package tree: $fileFull"
        }
        "$($fileFull.Substring($packagePrefix.Length))`t$($_.Length)"
    }
Set-Content -LiteralPath $manifestPath -Encoding UTF8 -Value @(
    "Package: $PackageName",
    "Created: $(Get-Date -Format o)",
    "Source commit: $(git -C (Resolve-RepoPath '.') rev-parse --short HEAD)",
    "ModelSourceRoot: $modelSourcePath",
    "",
    "Files:",
    $files
)

if (Test-Path -LiteralPath $finalPackageDir) {
    Remove-Item -LiteralPath $finalPackageDir -Recurse -Force
}
Move-Item -LiteralPath $stagingPackageDir -Destination $finalPackageDir
Remove-Item -LiteralPath $stagingRoot -Recurse -Force

$zipFullPath = $null
if ($Zip) {
    if ([string]::IsNullOrWhiteSpace($ZipPath)) {
        $ZipPath = Join-Path $outputRootPath ($PackageName + ".zip")
    }
    $zipFullPath = [System.IO.Path]::GetFullPath($ZipPath)
    if (Test-Path -LiteralPath $zipFullPath) { Remove-Item -LiteralPath $zipFullPath -Force }
    Compress-Archive -LiteralPath $finalPackageDir -DestinationPath $zipFullPath -CompressionLevel Optimal
}

[pscustomobject]@{
    PackageDir = [System.IO.Path]::GetFullPath($finalPackageDir)
    ZipPath = $zipFullPath
    FileCount = (Get-ChildItem -LiteralPath $finalPackageDir -Recurse -File | Measure-Object).Count
    ByteCount = (Get-ChildItem -LiteralPath $finalPackageDir -Recurse -File | Measure-Object Length -Sum).Sum
}

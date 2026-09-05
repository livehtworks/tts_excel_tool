param(
    [string]$OutputRoot = "dist/source",
    [string]$PackageName = "AdayoCorpusTool",
    [string]$ZipPath = ""
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$PathValue"))
}

$repo = Resolve-RepoPath "."
$outputRootPath = Resolve-RepoPath $OutputRoot
New-Item -ItemType Directory -Force -Path $outputRootPath | Out-Null

$short = (git -C $repo rev-parse --short HEAD).Trim()
if ([string]::IsNullOrWhiteSpace($short)) {
    throw "Unable to resolve git HEAD for source package"
}

if ([string]::IsNullOrWhiteSpace($ZipPath)) {
    $ZipPath = Join-Path $outputRootPath "$PackageName-source-$short-$(Get-Date -Format yyyyMMdd-HHmmss).zip"
}
$zipFullPath = [System.IO.Path]::GetFullPath($ZipPath)
$staging = Join-Path $outputRootPath ".source-staging-$short-$(Get-Date -Format yyyyMMdd-HHmmss)"
if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
New-Item -ItemType Directory -Force -Path $staging | Out-Null

$tracked = git -C $repo ls-files
if (-not $tracked) { throw "git ls-files returned no source files" }
$denyFragments = @(
    "/build/", "/build-", "/dist/", "/vcpkg_installed/", "/backup/", "/logs/", "/cache/", "/exports/",
    "/docs/audit/", "/docs/history/",
    "CMakeUserPresets.json", ".obj", ".pdb", ".exe", ".dll", ".corrupt-", ".tmp-"
)
foreach ($relative in $tracked) {
    $normalized = "/" + ($relative -replace "\\", "/")
    $denied = $false
    foreach ($fragment in $denyFragments) {
        if ($normalized.Contains($fragment)) { $denied = $true; break }
    }
    if ($denied) { continue }
    $source = Join-Path $repo $relative
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { continue }
    $target = Join-Path $staging $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $target -Parent) | Out-Null
    Copy-Item -LiteralPath $source -Destination $target -Force
}

$patterns = @(
    ("D" + ":/"),
    ("D" + ":\"),
    ("C" + ":/Users/"),
    ("C" + ":\Users\"),
    $env:USERNAME
)
$textExtensions = @(".txt",".md",".json",".cmake",".ps1",".py",".cpp",".h",".hpp",".cxx",".in",".xml",".manifest")
$hits = @()
Get-ChildItem -LiteralPath $staging -Recurse -File | Where-Object {
    $textExtensions -contains $_.Extension
} | ForEach-Object {
    $path = $_.FullName
    $content = Get-Content -LiteralPath $path -Encoding UTF8 -Raw
    foreach ($pattern in $patterns) {
        if (-not [string]::IsNullOrWhiteSpace($pattern) -and $content.Contains($pattern)) {
            $hits += "$path contains '$pattern'"
        }
    }
}
if ($hits.Count -gt 0) {
    throw "Source package personal-path scan failed:`n$($hits -join "`n")"
}

if (Test-Path -LiteralPath $zipFullPath) { Remove-Item -LiteralPath $zipFullPath -Force }
Compress-Archive -LiteralPath (Join-Path $staging "*") -DestinationPath $zipFullPath -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $zipFullPath -Algorithm SHA256).Hash
Remove-Item -LiteralPath $staging -Recurse -Force

[pscustomobject]@{
    ZipPath = $zipFullPath
    Sha256 = $hash
    SourceCommit = $short
    ByteCount = (Get-Item -LiteralPath $zipFullPath).Length
}

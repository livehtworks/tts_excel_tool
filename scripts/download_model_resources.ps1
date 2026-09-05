param(
    [string]$ModelRoot = "dist/AdayoCorpusTool/model",
    [int]$MaxWorkers = 8,
    [switch]$SkipPiper,
    [switch]$SkipMoss
)

$ErrorActionPreference = "Stop"

function Resolve-RepoPath([string]$PathValue) {
    if ([System.IO.Path]::IsPathRooted($PathValue)) {
        return [System.IO.Path]::GetFullPath($PathValue)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot "..\$PathValue"))
}

function Invoke-ModelScopeDownload([string[]]$Arguments) {
    & modelscope @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "modelscope download failed: $($Arguments -join ' ')"
    }
}

$modelRootPath = Resolve-RepoPath $ModelRoot
New-Item -ItemType Directory -Force -Path $modelRootPath | Out-Null

# Voice/model downloads must use domestic sources without the local proxy.
foreach ($name in @("HTTP_PROXY", "HTTPS_PROXY", "ALL_PROXY", "http_proxy", "https_proxy", "all_proxy")) {
    Remove-Item "Env:$name" -ErrorAction SilentlyContinue
}
$env:NO_PROXY = "*"
$env:no_proxy = "*"
$env:GIT_CONFIG_GLOBAL = "NUL"

if (-not $SkipPiper) {
    & python (Resolve-RepoPath "scripts/download_piper_voices_modelscope.py") `
        --local-dir (Join-Path $modelRootPath "piper/voices") `
        --batch-size 16
    if ($LASTEXITCODE -ne 0) {
        throw "Piper ModelScope voice download failed"
    }
}

if (-not $SkipMoss) {
    $mossRoot = Join-Path $modelRootPath "moss"

    Invoke-ModelScopeDownload @(
        "download", "openmoss/MOSS-TTS-Nano-100M-ONNX",
        "--local-dir", (Join-Path $mossRoot "MOSS-TTS-Nano-100M-ONNX"),
        "--max-workers", [string]$MaxWorkers
    )

    Invoke-ModelScopeDownload @(
        "download", "openmoss/MOSS-Audio-Tokenizer-Nano-ONNX",
        "--local-dir", (Join-Path $mossRoot "MOSS-Audio-Tokenizer-Nano-ONNX"),
        "--max-workers", [string]$MaxWorkers
    )
}

Get-ChildItem -LiteralPath $modelRootPath -Directory |
    Sort-Object Name |
    Select-Object Name, FullName

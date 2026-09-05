param([string]$TestRoot = (Join-Path ([IO.Path]::GetTempPath()) ('adayo-package-test-' + [Guid]::NewGuid().ToString('N'))))
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $TestRoot) { throw 'Test root must be new' }
New-Item -ItemType Directory -Path $TestRoot | Out-Null
$scriptPath = Join-Path $PSScriptRoot 'package_windows.ps1'
$tokens = $null; $errors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw $errors[0] }
foreach ($func in $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $false)) {
    . ([scriptblock]::Create($func.Extent.Text))
}
$source = Join-Path $TestRoot 'source [1] space'
$dest = Join-Path $TestRoot 'copied [2]'
New-Item -ItemType Directory -Path $source | Out-Null
[IO.File]::WriteAllText((Join-Path $source 'normal.txt'), 'fixture')
[IO.File]::WriteAllText((Join-Path $source '.hidden'), 'hidden fixture')
[IO.File]::SetAttributes((Join-Path $source '.hidden'), [IO.FileAttributes]::Hidden)
$unicodeName = ([string][char]0x4e2d) + ([string][char]0x6587) + '.txt'
[IO.File]::WriteAllText((Join-Path $source $unicodeName), 'unicode fixture')
Copy-TreeRequired $source $dest
if (@(Get-TreeDigest $dest).Count -ne 3) { throw 'Tree copy lost files' }
$rejected = $false
try { Get-Dependents (Join-Path $env:SystemRoot 'System32\cmd.exe') 'nonexistent.dll' | Out-Null } catch { $rejected = $true }
if (-not $rejected) { throw 'dumpbin failure was accepted' }
if (-not (Test-SystemApiSet 'api-ms-win-crt-convert-l1-1-0.dll')) { throw 'Actual UCRT API set did not resolve to System32' }
if (Test-SystemApiSet 'api-ms-win-nonexistent-adayo-l1-1-0.dll') { throw 'Nonexistent API set was accepted' }
$final = Join-Path $TestRoot 'AdayoCorpusTool'
foreach ($dir in @('config','exports','cache','model')) {
    New-Item -ItemType Directory -Path (Join-Path $final $dir) -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $final "$dir\sentinel"), $dir)
}
$before = @(Get-TreeDigest $final) -join "`n"
$rejected = $false
try { & $scriptPath -BuildDir $source -ModelSourceRoot $source -OutputRoot $TestRoot | Out-Null } catch { $rejected = $_.Exception.Message -like '*Create-only target already exists*' }
if (-not $rejected -or $before -cne (@(Get-TreeDigest $final) -join "`n")) { throw 'Existing target protection failed' }
Write-Output "PASS: literal tree copy, hidden/Unicode files, failed dumpbin, existing target sentinels. Evidence: $TestRoot"

param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$root=Join-Path ([IO.Path]::GetFullPath($EvidenceRoot)) ('cache-junction-'+[Guid]::NewGuid().ToString('N'))
if (Test-Path -LiteralPath $root) { throw 'Fixture root already exists' }
$target=Join-Path $root 'target'
New-Item -ItemType Directory -Path $target | Out-Null
[IO.File]::WriteAllText((Join-Path $target 'sentinel'),'KEEP',[Text.UTF8Encoding]::new($false))
$junction=Join-Path $root 'junction'
New-Item -ItemType Junction -Path $junction -Target $target | Out-Null
Write-Output $junction

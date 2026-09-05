param([Parameter(Mandatory=$true)][string]$ArtifactDirectory)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
function Read-ZipXml($Archive, [string]$Name) {
    $entry=$Archive.GetEntry($Name)
    if (-not $entry) { throw "Missing XML entry: $Name" }
    $stream=$entry.Open()
    try {
        $document=[xml]::new()
        $document.PreserveWhitespace=$true
        $document.Load($stream)
        return ,$document
    } finally { $stream.Dispose() }
}
$path=Join-Path $ArtifactDirectory '对比报告.xlsx'
$archive=[IO.Compression.ZipFile]::OpenRead($path)
try {
    $xml=Read-ZipXml $archive 'xl/sharedStrings.xml'
    $ns=[Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('s','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
    $goldens=@(
        @{ Text='温度设置为22度'; Red='2' },
        @{ Text='温度设置为23度'; Red='3' },
        @{ Text='删除字符ABC'; Red='B' }
    )
    foreach ($golden in $goldens) {
        $matched=$false
        foreach ($item in $xml.SelectNodes('/s:sst/s:si',$ns)) {
            $text=($item.SelectNodes('.//s:t',$ns) | ForEach-Object { $_.InnerText }) -join ''
            if ($text -cne $golden.Text) { continue }
            $red=($item.SelectNodes('s:r[s:rPr/s:color[@rgb="FFFF0000"]]/s:t',$ns) | ForEach-Object { $_.InnerText }) -join ''
            if ($red -cne $golden.Red) { throw "Rich text red fragments differ: $text" }
            $matched=$true
        }
        if (-not $matched) { throw "Original rich text missing: $($golden.Text)" }
    }
    'PASS: actual OOXML original strings and red run fragments'
} finally { $archive.Dispose() }
$path=Join-Path $ArtifactDirectory 'metric-report.xlsx'
$archive=[IO.Compression.ZipFile]::OpenRead($path)
try {
    $xml=Read-ZipXml $archive 'xl/worksheets/sheet1.xml'
    $ns=[Xml.XmlNamespaceManager]::new($xml.NameTable)
    $ns.AddNamespace('s','http://schemas.openxmlformats.org/spreadsheetml/2006/main')
    if ($xml.SelectSingleNode('/s:worksheet/s:sheetData/s:row/s:c[@r="G2"]/s:v',$ns).InnerText -ne '300') {
        throw 'CER > 100% was not retained numerically'
    }
    'PASS: actual OOXML CER numeric cell G2=300'
} finally { $archive.Dispose() }

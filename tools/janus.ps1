#requires -Version 5.1
<#
.SYNOPSIS
    Windows USB serial file roundtrip. Explicit paths, no automatic host collection.
.EXAMPLE
    .\janus.ps1 -Action ports
    .\janus.ps1 -Action put -Port COM7 -Source .\payload.bin
    .\janus.ps1 -Action get -Port COM7 -Source .\received.bin
    .\janus.ps1 -Action roundtrip -Port COM7 -Source .\in.bin -Destination .\out.bin
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory)][ValidateSet('ports','put','get','roundtrip')][string]$Action,
    [string]$Source,
    [string]$Destination,
    [string]$Port = 'COM1'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:Limit = 65536

$script:Crc32Table = [uint32[]]::new(256)
for ($i = 0; $i -lt 256; $i++) {
    $c = [uint32]$i
    for ($k = 0; $k -lt 8; $k++) {
        $lsb = $c -band 1
        $c = [uint32]($c -shr 1)
        if ($lsb) { $c = [uint32]($c -bxor 0xEDB88320) }
    }
    $script:Crc32Table[$i] = $c
}

function Get-Crc32 {
    param([byte[]]$Data)
    $c = [uint32]0xFFFFFFFF
    foreach ($b in $Data) {
        $idx = [int](($c -bxor [uint32]$b) -band 0xFF)
        $c = [uint32](($c -shr 8) -bxor $script:Crc32Table[$idx])
    }
    return [uint32]($c -bxor 0xFFFFFFFF)
}

function Resolve-FullPath {
    param([string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) { return $Path }
    return [System.IO.Path]::GetFullPath([System.IO.Path]::Combine((Get-Location).ProviderPath, $Path))
}

function Invoke-Request {
    param($SerialPort, [string]$Command)
    $SerialPort.Write($Command + "`n")
    try {
        $answer = $SerialPort.ReadLine().TrimEnd("`r")
    } catch [System.TimeoutException] {
        throw 'Timed out or incomplete response; reconnect and retry the transfer'
    }
    if ($answer.StartsWith('ERR')) { throw $answer }
    return $answer
}

function Assert-Response {
    param($SerialPort, [string]$Command, [string]$Expected)
    $answer = Invoke-Request -SerialPort $SerialPort -Command $Command
    if ($answer -ne $Expected) { throw "Unexpected response: '$answer'" }
}

function Send-DeviceFile {
    param($SerialPort, [byte[]]$Data)
    if ($Data.Length -gt $script:Limit) { throw 'File exceeds 65536 bytes' }
    $crc = Get-Crc32 $Data
    Assert-Response -SerialPort $SerialPort -Command ("BEGIN {0} {1}" -f $Data.Length, $crc) -Expected 'OK BEGIN'
    for ($offset = 0; $offset -lt $Data.Length; $offset += 256) {
        $count = [Math]::Min(256, $Data.Length - $offset)
        $chunk = New-Object byte[] $count
        [Array]::Copy($Data, $offset, $chunk, 0, $count)
        $hex = ([BitConverter]::ToString($chunk) -replace '-', '').ToLower()
        Assert-Response -SerialPort $SerialPort -Command ("DATA {0} {1}" -f $offset, $hex) -Expected ("OK DATA {0}" -f ($offset + $count))
    }
    Assert-Response -SerialPort $SerialPort -Command 'COMMIT' -Expected 'OK COMMIT'
}

function ConvertFrom-HexChunk {
    param([string]$Hex, [int]$Count)
    if ($Hex.Length -ne $Count * 2) { throw 'Invalid chunk length' }
    $bytes = New-Object byte[] $Count
    for ($i = 0; $i -lt $Count; $i++) {
        $bytes[$i] = [Convert]::ToByte($Hex.Substring($i * 2, 2), 16)
    }
    return ,$bytes
}

function Receive-DeviceFile {
    param($SerialPort)
    $info = Invoke-Request -SerialPort $SerialPort -Command 'INFO'
    $fields = $info -split ' '
    if ($fields.Count -ne 3 -or $fields[0] -ne 'FILE') { throw 'Invalid file metadata' }
    $size = [int]$fields[1]
    $checksum = [uint32]$fields[2]
    if ($size -lt 0 -or $size -gt $script:Limit) { throw 'Invalid file size' }
    $result = New-Object byte[] $size
    for ($offset = 0; $offset -lt $size; $offset += 256) {
        $count = [Math]::Min(256, $size - $offset)
        $reply = Invoke-Request -SerialPort $SerialPort -Command ("READ {0} {1}" -f $offset, $count)
        if (-not $reply.StartsWith('DATA ')) { throw 'Invalid chunk response' }
        $chunk = ConvertFrom-HexChunk -Hex $reply.Substring(5) -Count $count
        [Array]::Copy($chunk, 0, $result, $offset, $count)
    }
    if ((Get-Crc32 $result) -ne $checksum) { throw 'CRC mismatch' }
    return ,$result
}

function Show-SdStatus {
    param($SerialPort)
    $SerialPort.Write("SDINFO`n")
    try {
        $reply = $SerialPort.ReadLine().TrimEnd("`r")
    } catch [System.TimeoutException] {
        Write-Output 'SD status unknown (timeout)'
        return
    }
    if ($reply -like 'SD *') {
        Write-Output ("SD write: {0}" -f $reply.Substring(3))
    } elseif ($reply -eq 'ERR SD') {
        Write-Output 'SD not written (no card or write failed)'
    } else {
        Write-Output ("SD status: {0}" -f $reply)
    }
}

function Show-Ports {
    $names = [System.IO.Ports.SerialPort]::GetPortNames() | Sort-Object
    $descriptions = @{}
    try {
        Get-CimInstance -ClassName Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.Name -match '\((COM\d+)\)' } |
            ForEach-Object { $descriptions[$Matches[1]] = $_.Name }
    } catch { }
    foreach ($n in $names) {
        $desc = if ($descriptions.ContainsKey($n)) { $descriptions[$n] } else { $n }
        Write-Output ("{0}: {1}" -f $n, $desc)
    }
}

function Save-Exclusive {
    param([string]$Path, [byte[]]$Data)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::CreateNew, [System.IO.FileAccess]::Write)
    try { $stream.Write($Data, 0, $Data.Length) } finally { $stream.Dispose() }
}

function Compare-Bytes {
    param([byte[]]$A, [byte[]]$B)
    if ($A.Length -ne $B.Length) { return $false }
    for ($i = 0; $i -lt $A.Length; $i++) {
        if ($A[$i] -ne $B[$i]) { return $false }
    }
    return $true
}

if ($Action -eq 'ports') {
    Show-Ports
    return
}

if (-not $Source) { throw 'A file path is required' }
if ($Action -eq 'roundtrip' -and -not $Destination) { throw 'roundtrip requires input and output paths' }

$Source = Resolve-FullPath $Source
if ($Destination) { $Destination = Resolve-FullPath $Destination }
$output = if ($Action -eq 'roundtrip') { $Destination } else { $Source }
if ($Action -ne 'put' -and (Test-Path -LiteralPath $output)) {
    throw ("Output already exists: {0}" -f $output)
}

$serial = [System.IO.Ports.SerialPort]::new($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$serial.ReadTimeout = 5000
$serial.WriteTimeout = 5000
$serial.NewLine = "`n"
$serial.Encoding = [System.Text.Encoding]::ASCII
$serial.Open()
try {
    $serial.DiscardInBuffer()
    Assert-Response -SerialPort $serial -Command 'HELLO' -Expected 'JANUS 1 65536'
    Assert-Response -SerialPort $serial -Command 'CLIENT windows' -Expected 'OK CLIENT'

    $original = $null
    if ($Action -eq 'put' -or $Action -eq 'roundtrip') {
        if ((Get-Item -LiteralPath $Source).Length -gt $script:Limit) { throw 'File exceeds 65536 bytes' }
        $original = [System.IO.File]::ReadAllBytes($Source)
        Send-DeviceFile -SerialPort $serial -Data $original
        Show-SdStatus -SerialPort $serial
    }
    if ($Action -eq 'get' -or $Action -eq 'roundtrip') {
        $received = Receive-DeviceFile -SerialPort $serial
        if ($null -ne $original -and -not (Compare-Bytes $received $original)) {
            throw 'Roundtrip byte comparison failed'
        }
        Save-Exclusive -Path $output -Data $received
    }
    Write-Output 'Transfer verified successfully'
} finally {
    $serial.Close()
    $serial.Dispose()
}

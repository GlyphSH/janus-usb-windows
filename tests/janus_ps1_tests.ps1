#requires -Version 5.1
<#
.SYNOPSIS
    Unit tests for tools/janus.ps1. Runs in Windows PowerShell 5.1 (where the
    0xFFFFFFFF integer-literal overflow originally hit) and in PowerShell 7+.
.DESCRIPTION
    Dot-sources janus.ps1 with JANUS_PS1_TEST_MODE set so the main action
    dispatch returns before touching a serial port, then checks Get-Crc32
    against a canonical corpus pinned by tests/transfer_test.cpp and
    tests/test_client.py.
#>
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$janus = Join-Path $PSScriptRoot '..\tools\janus.ps1' | Resolve-Path
$env:JANUS_PS1_TEST_MODE = '1'
try {
    . $janus.Path -Action ports
} finally {
    Remove-Item Env:\JANUS_PS1_TEST_MODE -ErrorAction SilentlyContinue
}

# @{Name=..; Data=byte[]; Expected=uint32}. Keep in lockstep with the
# equivalent list in tests/test_client.py and tests/transfer_test.cpp.
$vectors = @(
    @{Name='empty';     Data=[byte[]]@();                                                   Expected=[uint32]'0x00000000'}
    @{Name='a';         Data=[System.Text.Encoding]::ASCII.GetBytes('a');                   Expected=[uint32]'0xe8b7be43'}
    @{Name='abc';       Data=[System.Text.Encoding]::ASCII.GetBytes('abc');                 Expected=[uint32]'0x352441c2'}
    @{Name='123456789'; Data=[System.Text.Encoding]::ASCII.GetBytes('123456789');           Expected=[uint32]'0xcbf43926'}
    @{Name='fox';       Data=[System.Text.Encoding]::ASCII.GetBytes(
                              'The quick brown fox jumps over the lazy dog');               Expected=[uint32]'0x414fa339'}
    @{Name='ff256';     Data=[byte[]]@(0..255 | ForEach-Object { 0xff });                   Expected=[uint32]'0xfea8a821'}
    @{Name='range256';  Data=[byte[]]@(0..255);                                             Expected=[uint32]'0x29058c73'}
)

$failed = 0
foreach ($v in $vectors) {
    $actual = Get-Crc32 -Data $v.Data
    if ($actual -ne $v.Expected) {
        Write-Output ("FAIL {0}: expected 0x{1:x8}, got 0x{2:x8}" -f $v.Name, $v.Expected, $actual)
        $failed++
    } else {
        Write-Output ("OK   {0,-10} 0x{1:x8}" -f $v.Name, $actual)
    }
}
if ($failed -ne 0) { throw ("Get-Crc32 mismatched on {0} vector(s)" -f $failed) }

# Hex-decoding helper roundtrip: ConvertFrom-HexChunk is used on every
# downloaded chunk, so its own correctness is a dependency of Get-Crc32's
# usefulness in the wild.
$roundtrip = ConvertFrom-HexChunk -Hex 'deadbeef' -Count 4
if (-not (Compare-Bytes -A $roundtrip -B ([byte[]]@(0xde, 0xad, 0xbe, 0xef)))) {
    throw 'ConvertFrom-HexChunk roundtrip mismatch'
}
Write-Output 'OK   ConvertFrom-HexChunk roundtrip'

try {
    [void](ConvertFrom-HexChunk -Hex 'zz' -Count 1)
    throw 'ConvertFrom-HexChunk should have rejected non-hex input'
} catch [System.FormatException] {
    Write-Output 'OK   ConvertFrom-HexChunk rejects non-hex'
}

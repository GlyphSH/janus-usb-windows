#requires -Version 5.1
<#
.SYNOPSIS
    Reads C:\test.txt (creating it if absent), transfers it to the dongle over
    USB serial, and reports whether the dongle wrote it to the SD card.
    Intended as a short walkthrough for colleagues.
.EXAMPLE
    .\demo-sd.ps1
    .\demo-sd.ps1 -Port COM8
    .\demo-sd.ps1 -Port COM7 -Source D:\payloads\hello.txt
#>
[CmdletBinding()]
param(
    [string]$Port = 'COM7',
    [string]$Source = 'C:\test.txt'
)

$ErrorActionPreference = 'Stop'
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path

Write-Host ''
Write-Host 'Janus SD demo' -ForegroundColor Cyan
Write-Host ('  Port:   {0}' -f $Port)
Write-Host ('  Source: {0}' -f $Source)
Write-Host ''

if (-not (Test-Path -LiteralPath $Source)) {
    $stamp = Get-Date -Format 's'
    "Hello from Janus demo at $stamp" | Out-File -LiteralPath $Source -Encoding ASCII
    Write-Host ('  (created {0} for the demo)' -f $Source) -ForegroundColor Yellow
    Write-Host ''
}

& (Join-Path $scriptDir 'janus.ps1') -Action put -Port $Port -Source $Source

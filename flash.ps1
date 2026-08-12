# One-click flash: builds (if needed) and uploads over serial, then monitors.
param(
    [Parameter(Mandatory = $true)]
    [string]$Port,
    [int]$Baud = 115200
)
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here "export.ps1")
idf.py -p $Port -b $Baud flash monitor
exit $LASTEXITCODE

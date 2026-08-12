# One-click build: sets up the toolchain environment and compiles.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
. (Join-Path $here "export.ps1")
idf.py build
exit $LASTEXITCODE
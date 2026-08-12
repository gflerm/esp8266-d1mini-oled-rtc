# Dot-source this script to set up the ESP8266_RTOS_SDK environment in the
# current PowerShell session, then run idf.py yourself, e.g.:
#
#   . .\export.ps1
#   idf.py build
#   idf.py -p COMxx flash monitor

if ([string]::IsNullOrWhiteSpace($env:IDF_PATH)) {
    throw "Set IDF_PATH to your ESP8266_RTOS_SDK directory before sourcing export.ps1."
}
if (-not (Test-Path $env:IDF_PATH)) {
    throw "ESP8266_RTOS_SDK not found at $env:IDF_PATH"
}

$python = $env:ESP8266_PYTHON
if ([string]::IsNullOrWhiteSpace($python) -and $env:IDF_TOOLS_PATH) {
    $python = Join-Path $env:IDF_TOOLS_PATH "python\esp8266\Scripts\python.exe"
}
if ([string]::IsNullOrWhiteSpace($python) -or -not (Test-Path $python)) {
    throw "Set ESP8266_PYTHON to the ESP8266 Python executable."
}

$toolRoot = $env:ESP8266_TOOL_ROOT
if ([string]::IsNullOrWhiteSpace($toolRoot) -and $env:IDF_TOOLS_PATH) {
    $toolRoot = Join-Path $env:IDF_TOOLS_PATH "tools"
}
if ([string]::IsNullOrWhiteSpace($toolRoot) -or -not (Test-Path $toolRoot)) {
    throw "Set ESP8266_TOOL_ROOT to the directory containing the installed ESP8266 tools."
}

$env:PATH = @(
    "$toolRoot\xtensa-lx106-elf\esp-2020r3-49-gd5524c1-8.4.0\xtensa-lx106-elf\bin",
    "$toolRoot\cmake\3.13.4\bin",
    "$toolRoot\ninja\1.9.0",
    "$toolRoot\idf-exe\1.0.1",
    "$toolRoot\mconf\v4.6.0.0-idf-20190628",
    "$env:PATH"
) -join ";"

function idf.py {
    & $python "$env:IDF_PATH\tools\idf.py" @args
}

# Dot-source this script to set up the ESP8266_RTOS_SDK environment in the
# current PowerShell session, then run idf.py yourself, e.g.:
#
#   . .\export.ps1
#   idf.py build
#   idf.py -p COM3 flash monitor

$env:IDF_PATH = "C:\esp\ESP8266_RTOS_SDK"
$python = "C:\Espressif\tools\python\esp8266\Scripts\python.exe"
if (-not (Test-Path $python)) { throw "Python venv not found at $python" }

$toolRoot = "C:\Espressif\tools\tools"
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
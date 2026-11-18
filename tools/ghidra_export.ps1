# Exporta as funções do sh3.exe com o Ghidra (sem interface) para re/out/functions.json.
# Uso: powershell -File tools/ghidra_export.ps1 (leva alguns minutos)
# Caminhos: SH3_GHIDRA (pasta do Ghidra) e SH3_JAVA_HOME (JDK 25: o Ghidra 12.2 recusa versões anteriores); os padrões ficam em ~/tools.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$ghidra = if ($env:SH3_GHIDRA) { $env:SH3_GHIDRA } else { "$env:USERPROFILE\tools\ghidra_12.2_PUBLIC" }
$env:JAVA_HOME = if ($env:SH3_JAVA_HOME) { $env:SH3_JAVA_HOME } else { "C:\Program Files\Microsoft\jdk-25.0.5.21-hotspot" }
$exe = Join-Path $root "game\sh3.exe"
$out = Join-Path $root "re\out\functions.json"
$proj = Join-Path $env:TEMP "sh3vita-ghidra"
# Configurações e cache do Ghidra fora do AppData: dentro de apps empacotados (MSIX), AppData é redirecionado
# para a pasta do pacote, os caminhos canônicos divergem e o OSGi/Felix aborta.
$ghome = if ($env:SH3_GHIDRA_HOME) { $env:SH3_GHIDRA_HOME } else { "$env:USERPROFILE\tools\ghidra-home" }
New-Item -ItemType Directory -Force (Join-Path $ghome "settings"), (Join-Path $ghome "cache") | Out-Null
$env:JAVA_TOOL_OPTIONS = "-Dapplication.settingsdir=$ghome\settings -Dapplication.cachedir=$ghome\cache"

& (Join-Path $root ".venv\Scripts\python.exe") (Join-Path $root "tools\sh3exe.py") $exe
if ($LASTEXITCODE -ne 0) { throw "sh3.exe não suportado" }
New-Item -ItemType Directory -Force (Split-Path $out) | Out-Null
New-Item -ItemType Directory -Force $proj | Out-Null
& (Join-Path $ghidra "support\analyzeHeadless.bat") $proj sh3 -import $exe -overwrite `
    -scriptPath (Join-Path $root "re\ghidra") -postScript ExportFunctions.java $out -deleteProject
if ($LASTEXITCODE -ne 0) { throw "analyzeHeadless falhou ($LASTEXITCODE)" }
Write-Output "ok: $out"

# Copie Open Filter.vst3 dans le dossier VST3 système (utilisé par Ableton Live).
# À lancer dans un terminal administrateur :
#   powershell -ExecutionPolicy Bypass -File scripts\install.ps1
# Chemin du .vst3 en argument (par défaut : la compilation locale en Release).
param(
    [string]$Source = "build\OpenFilter_artefacts\Release\VST3\Open Filter.vst3"
)

$ErrorActionPreference = "Stop"
$dest = Join-Path $env:CommonProgramFiles "VST3"

$identity = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
if (-not $identity.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    Write-Error "Ce script doit être lancé dans un terminal administrateur (la copie vers '$dest' l'exige)."
}

if (-not (Test-Path $Source)) {
    Write-Error "Introuvable : $Source. Compile d'abord le plugin, ou indique le chemin du .vst3 en argument."
}

if (Get-Process -Name "Ableton Live*" -ErrorAction SilentlyContinue) {
    Write-Warning "Ableton Live est ouvert : ferme-le avant d'installer, sinon la copie peut échouer."
}

$target = Join-Path $dest (Split-Path $Source -Leaf)
if (Test-Path $target) { Remove-Item -Recurse -Force $target }
Copy-Item -Recurse -Force $Source $dest
Write-Host "Installé : $target"
Write-Host "Dans Live : Options > Préférences > Plug-ins > Utiliser les dossiers système VST3, puis Réanalyser."

# Downloads build-only tools. System installation is an explicit opt-in; there
# are no purchases, global PATH edits, automatic restarts or project dependencies.
[CmdletBinding()]
param([switch]$InstallVisualStudio)

$ErrorActionPreference = 'Stop'
if (-not $IsWindows) { throw 'This setup script is for Windows with PowerShell 7.' }
$repoRoot = Split-Path -Parent $PSScriptRoot
$pins = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows-toolchain.json') -Raw | ConvertFrom-Json
$downloads = Join-Path $repoRoot 'build/downloads'
$tools = Join-Path $repoRoot 'build/tools'
New-Item -ItemType Directory -Path $downloads, $tools -Force | Out-Null

function Get-PinnedDownload($pin) {
    $destination = Join-Path $downloads $pin.archive
    if (-not (Test-Path -LiteralPath $destination)) {
        Invoke-WebRequest -Uri $pin.url -OutFile $destination
    }
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($hash -ne $pin.sha256) {
        throw "Hash mismatch for $destination. Do not execute or extract this file."
    }
    Write-Host "Verified $($pin.archive)"
    return $destination
}

$cmakeArchive = Get-PinnedDownload $pins.cmake
$cmakeRoot = Join-Path $tools "cmake-$($pins.cmake.version)-windows-x86_64"
if (-not (Test-Path -LiteralPath $cmakeRoot)) {
    Expand-Archive -LiteralPath $cmakeArchive -DestinationPath $tools
}
$validatorArchive = Get-PinnedDownload $pins.pluginval
$validatorRoot = Join-Path $tools "pluginval-$($pins.pluginval.version)"
if (-not (Test-Path -LiteralPath $validatorRoot)) {
    Expand-Archive -LiteralPath $validatorArchive -DestinationPath $validatorRoot
}

if ($InstallVisualStudio) {
    $installer = Get-PinnedDownload $pins.visualStudio
    $signature = Get-AuthenticodeSignature -LiteralPath $installer
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Microsoft Corporation') {
        throw 'Visual Studio installer signature is not valid for Microsoft Corporation.'
    }
    $config = Join-Path $PSScriptRoot $pins.visualStudio.components
    $arguments = '--quiet --wait --norestart --installPath "{0}" --config "{1}" --addProductLang en-US' -f $pins.visualStudio.installPath, $config
    $installation = Start-Process -FilePath $installer -ArgumentList $arguments `
        -WindowStyle Hidden -Wait -PassThru
    if ($installation.ExitCode -notin @(0, 3010)) {
        throw "Visual Studio installation failed (exit $($installation.ExitCode)). See its dd_* setup logs."
    }
    if ($installation.ExitCode -eq 3010) {
        Write-Warning 'Installer requests a restart. No restart has been performed.'
    }
    Write-Output "Visual Studio installer exit: $($installation.ExitCode)"
}
Write-Output 'Portable tools ready. Run tools/check-windows-toolchain.ps1 after the Microsoft tools are installed.'

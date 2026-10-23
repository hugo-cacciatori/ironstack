# Installs only the already validated M1 audition build. Run with -VerifyOnly first.
[CmdletBinding()]
param([switch]$VerifyOnly)

$ErrorActionPreference = 'Stop'
$package = Join-Path $PSScriptRoot '../build/audition/Ironstack-0.1.0-windows-x64-9c238730'
$source = Join-Path $package 'Ironstack.vst3'
$relativeBinary = 'Contents/x86_64-win/Ironstack.vst3'
$expectedHash = 'ce47604db1baf49424d7dc65561ab9c5d2d739591acf72b4700da8fe74780f1c'
if (-not (Test-Path -LiteralPath $source -PathType Container)) {
    throw "Prepared audition package missing: $package"
}
$sourceHash = (Get-FileHash -LiteralPath (Join-Path $source $relativeBinary) -Algorithm SHA256).Hash
if ($sourceHash -ne $expectedHash) { throw 'Source VST3 differs from the validated audition binary.' }
$sourceFiles = @(Get-ChildItem -LiteralPath $source -File -Recurse)
$sourceRoot = (Resolve-Path -LiteralPath $source).Path
$commonFiles = [Environment]::GetFolderPath('CommonProgramFiles')
if (-not [Environment]::Is64BitProcess -or [string]::IsNullOrWhiteSpace($commonFiles)) {
    throw 'Run this script in 64-bit PowerShell on Windows.'
}
$pluginRoot = Join-Path $commonFiles 'VST3'
$destination = Join-Path $pluginRoot 'Ironstack.vst3'
Write-Output "Verified M1 binary: 9c238730162966293a997ab995ce7762282537e8"
Write-Output "Install destination: $destination"
if ($VerifyOnly) { Write-Output 'Verification only; no installation performed.'; return }

if (Test-Path -LiteralPath $destination) {
    # Do not replace an existing installation or remove any files.
    $installedFiles = @(Get-ChildItem -LiteralPath $destination -File -Recurse)
    if ($installedFiles.Count -ne $sourceFiles.Count) {
        throw 'An existing Ironstack installation differs. No files were changed.'
    }
    foreach ($file in $sourceFiles) {
        $relative = $file.FullName.Substring($sourceRoot.Length + 1)
        $installed = Join-Path $destination $relative
        if (-not (Test-Path -LiteralPath $installed -PathType Leaf)) {
            throw "Existing installation differs: $relative. No files were changed."
        }
        if ((Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Existing installation differs: $relative. No files were changed."
        }
    }
    Write-Output 'This exact audition bundle is already installed.'
    return
}

$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw 'Open 64-bit PowerShell as Administrator, then run this command again.'
}
New-Item -ItemType Directory -Path $pluginRoot -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $destination -Recurse
foreach ($file in $sourceFiles) {
    $relative = $file.FullName.Substring($sourceRoot.Length + 1)
    $installed = Join-Path $destination $relative
    if ((Get-FileHash -LiteralPath $installed).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
        throw "Installed checksum mismatch: $relative. Stop and report this error."
    }
}
Write-Output 'Installed and verified. Open your DAW and scan for Ironstack (VST3 instrument).'

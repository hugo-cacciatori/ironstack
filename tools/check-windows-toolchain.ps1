# Builds an isolated readiness probe; this does not build or validate a plugin.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pins = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows-toolchain.json') -Raw | ConvertFrom-Json
$cmakeRoot = Join-Path $repoRoot "build/tools/cmake-$($pins.cmake.version)-windows-x86_64/bin"
$cmake = Join-Path $cmakeRoot 'cmake.exe'
$ctest = Join-Path $cmakeRoot 'ctest.exe'
$pluginval = Join-Path $repoRoot "build/tools/pluginval-$($pins.pluginval.version)/pluginval.exe"
$probeBuild = Join-Path $repoRoot 'build/nmake-toolchain-probe'
$developerShell = Join-Path $pins.visualStudio.installPath 'Common7/Tools/VsDevCmd.bat'

foreach ($toolPath in @($cmake, $ctest, $pluginval, $developerShell)) {
    if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
        throw "Missing tool: $toolPath. Follow docs/build.md to provision it."
    }
}

& $cmake --version
if ($LASTEXITCODE -ne 0) { throw 'CMake did not start.' }
& $ctest --version
if ($LASTEXITCODE -ne 0) { throw 'CTest did not start.' }

# CMake's Visual Studio generator did not discover this installation. Use the
# installed developer shell and NMake instead; this needs no extra modules and
# keeps compiler/SDK environment changes inside a child process.
New-Item -ItemType Directory -Path $probeBuild -Force | Out-Null
$probeScript = Join-Path $probeBuild 'run-probe.cmd'
$batchLines = @(
    '@echo off'
    ('call "{0}" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver={1}' -f $developerShell, $pins.visualStudio.toolset)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release' -f $cmake, (Join-Path $PSScriptRoot 'toolchain_probe'), $probeBuild)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" --build "{1}"' -f $cmake, $probeBuild)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" --test-dir "{1}" --output-on-failure' -f $ctest, $probeBuild)
    'exit /b %errorlevel%'
)
[System.IO.File]::WriteAllLines($probeScript, $batchLines, [System.Text.UTF8Encoding]::new($false))
& $env:ComSpec /d /c $probeScript
if ($LASTEXITCODE -ne 0) { throw 'MSVC/Windows SDK configure, build or execution failed.' }

# Explicitly wait for this Windows GUI-subsystem executable, even in shells
# where invoking it directly does not populate LASTEXITCODE reliably.
$versionLog = Join-Path $probeBuild 'pluginval-version.txt'
$versionErrors = Join-Path $probeBuild 'pluginval-version-errors.txt'
$validator = Start-Process -FilePath $pluginval -ArgumentList '--version' `
    -WindowStyle Hidden -Wait -PassThru `
    -RedirectStandardOutput $versionLog -RedirectStandardError $versionErrors
Get-Content -LiteralPath $versionLog
if ($validator.ExitCode -ne 0) {
    Get-Content -LiteralPath $versionErrors
    throw "pluginval did not start successfully (exit $($validator.ExitCode))."
}
Write-Output 'Windows toolchain readiness passed. Plugin validation is still a separate milestone check.'

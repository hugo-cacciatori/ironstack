# Build the instrument in an isolated MSVC/SDK environment, then run CTest.
[CmdletBinding()]
param(
    [string]$JuceSource = ''
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$pins = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'windows-toolchain.json') -Raw | ConvertFrom-Json
$cmakeRoot = Join-Path $repoRoot "build/tools/cmake-$($pins.cmake.version)-windows-x86_64/bin"
$cmake = Join-Path $cmakeRoot 'cmake.exe'
$ctest = Join-Path $cmakeRoot 'ctest.exe'
$buildRoot = Join-Path $repoRoot 'build/windows-release'
$developerShell = Join-Path $pins.visualStudio.installPath 'Common7/Tools/VsDevCmd.bat'
foreach ($toolPath in @($cmake, $ctest, $developerShell)) {
    if (-not (Test-Path -LiteralPath $toolPath -PathType Leaf)) {
        throw "Missing tool: $toolPath. Follow docs/build.md to provision it."
    }
}

$sourceOption = ''
$resolvedSource = ''
if ($JuceSource) {
    $resolvedSource = (Resolve-Path -LiteralPath $JuceSource).Path
    if (-not (Test-Path -LiteralPath (Join-Path $resolvedSource 'CMakeLists.txt'))) {
        throw 'JuceSource must point to the pinned JUCE source root.'
    }
    $sourceOption = ' "-DFETCHCONTENT_SOURCE_DIR_JUCE={0}"' -f $resolvedSource
} else {
    # Clear any previous offline override when explicitly using automatic fetching.
    $sourceOption = ' -DFETCHCONTENT_SOURCE_DIR_JUCE='
}

# These strings become cmd.exe input. Reject metacharacters rather than allowing
# an unusual checkout/source path to be interpreted as another command.
foreach ($batchValue in @($repoRoot, $resolvedSource, $developerShell, $pins.visualStudio.toolset)) {
    if ($batchValue -match '["%&|<>^!\r\n]') { throw 'Unsupported cmd.exe character in build path/toolset.' }
}
New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null
$batchScript = Join-Path $buildRoot 'run-build.cmd'
$batchLines = @(
    '@echo off'
    'setlocal DisableDelayedExpansion'
    ('call "{0}" -no_logo -arch=x64 -host_arch=x64 -vcvars_ver={1}' -f $developerShell, $pins.visualStudio.toolset)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" -S "{1}" -B "{2}" -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON{3}' -f $cmake, $repoRoot, $buildRoot, $sourceOption)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" --build "{1}"' -f $cmake, $buildRoot)
    'if errorlevel 1 exit /b %errorlevel%'
    ('"{0}" --test-dir "{1}" --output-on-failure' -f $ctest, $buildRoot)
    'exit /b %errorlevel%'
)
[System.IO.File]::WriteAllLines($batchScript, $batchLines, [System.Text.UTF8Encoding]::new($false))
& (Join-Path $PSScriptRoot 'maintain-storage.ps1') -Apply
try {
    & $env:ComSpec /d /c $batchScript
    if ($LASTEXITCODE -ne 0) { throw 'Ironstack configure, build or CTest failed.' }
} finally {
    & (Join-Path $PSScriptRoot 'maintain-storage.ps1') -Apply
}

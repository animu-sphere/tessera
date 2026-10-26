# Core-only configure/build/test phases shared by CI and local measurements.
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-ci',
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Debug',
    [ValidateSet('pr', 'full', 'release')][string]$Suite = 'full',
    [ValidateRange(1, 64)][int]$BuildParallelism = 2,
    [ValidateRange(1, 64)][int]$TestParallelism = 1
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$reused = Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt')
$artifacts = Join-Path $build "ci-$($Configuration.ToLowerInvariant())-$Suite"
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
$phases = New-Object 'System.Collections.Generic.List[object]'
$context = [ordered]@{
    configuration = $Configuration; suite = $Suite; reused_build = $reused
    build_parallelism = $BuildParallelism; test_parallelism = $TestParallelism
    started_utc = [datetime]::UtcNow.ToString('o'); source = $repo; build = $build
    phases = $phases
}
function Invoke-Phase([string]$Name, [string]$Program, [string[]]$Arguments) {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    # Windows PowerShell turns native stderr into ErrorRecords. Preserve those
    # as log lines and use the native exit code, including CTest's failure code.
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath (Join-Path $artifacts "$Name.log")
        $nativeExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
        $watch.Stop()
    }
    $phases.Add([ordered]@{ name = $Name; command = $Program; arguments = $Arguments
        seconds = $watch.Elapsed.TotalSeconds; exit_code = $nativeExit })
    $context | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 (Join-Path $artifacts 'phases.json')
    if ($nativeExit -ne 0) { throw "$Name failed with exit code $nativeExit; see $artifacts" }
}
Get-Command cmake, ctest -ErrorAction Stop | Out-Null
$context['cmake_version'] = @(& cmake --version)[0]
$context['ctest_version'] = @(& ctest --version)[0]
if (Get-Command git -ErrorAction SilentlyContinue) {
    $context['revision'] = & git -C $repo rev-parse HEAD
    $context['worktree_changes'] = @(& git -C $repo status --short)
}
Invoke-Phase 'configure' 'cmake' @('-S', $repo, '-B', $build, '-A', 'x64',
    '-DBUILD_TESTING=ON', '-DTESSERA_BUILD_TESTING=ON', '-DTESSERA_BUILD_EXAMPLES=ON',
    '-DTESSERA_BUILD_VULKAN=OFF', '-DTESSERA_BUILD_FONTS=OFF')
Invoke-Phase 'build' 'cmake' @('--build', $build, '--config', $Configuration,
    '--parallel', "$BuildParallelism", '--', '/nodeReuse:false')
$selection = @()
if ($Suite -eq 'pr') { $selection = @('-L', '^(contract|smoke)$') }
# Preserve the release workflow's existing FetchContent exclusion. Full runs
# execute both import modes, with no test filter.
if ($Suite -eq 'release') { $selection = @('-E', '_FetchContent$') }
& ctest --test-dir $build -C $Configuration --show-only=json-v1 @selection |
    Set-Content -Encoding utf8 (Join-Path $artifacts 'inventory.json')
if ($LASTEXITCODE -ne 0) { throw 'CTest inventory failed' }
Invoke-Phase 'test' 'ctest' (@('--test-dir', $build, '-C', $Configuration,
    '--output-on-failure', '--no-tests=error', '-j', "$TestParallelism",
    '--output-junit', (Join-Path $artifacts 'ctest.xml'),
    '--output-log', (Join-Path $artifacts 'ctest.log')) + $selection)
Copy-Item -LiteralPath (Join-Path $build 'Testing/Temporary/CTestCostData.txt') -Destination $artifacts
if ($env:GITHUB_STEP_SUMMARY) {
    "### Core $Configuration / $Suite`n`n| Phase | Seconds | Exit |`n| --- | ---: | ---: |" >> $env:GITHUB_STEP_SUMMARY
    foreach ($phase in $phases) {
        "| $($phase.name) | $([math]::Round($phase.seconds, 2)) | $($phase.exit_code) |" >> $env:GITHUB_STEP_SUMMARY
    }
}
Write-Output "Core CI artifacts: $artifacts"

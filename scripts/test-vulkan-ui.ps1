[CmdletBinding()]
param(
    [string]$BuildDirectory = 'build-ui-vulkan',
    [ValidateCount(1, 2)][ValidateSet('Debug', 'Release')][string[]]$Configuration = @('Debug', 'Release'),
    [ValidateRange(1, 64)][int]$BuildParallelism = 4,
    [string[]]$CMakeArguments = @()
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This suite includes the Win32 presentation smokes and requires Windows.'
}
if (@($Configuration | Sort-Object -Unique).Count -ne $Configuration.Count) {
    throw 'Select each configuration only once.'
}
Get-Command cmake, ctest, git -ErrorAction Stop | Out-Null
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$build = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($BuildDirectory)
$run = Join-Path $build ("gpu-regression-" + [datetime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ') + '-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $run -Force | Out-Null
$phases = New-Object 'System.Collections.Generic.List[object]'
$context = [ordered]@{
    status = 'running'; started_utc = [datetime]::UtcNow.ToString('o')
    source = $repo; build = $build; configurations = @($Configuration)
    reused_build = (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))
    cmake_arguments = @($CMakeArguments); build_parallelism = $BuildParallelism
    operating_system = [Environment]::OSVersion.VersionString
    revision = (& git -C $repo rev-parse HEAD); worktree_changes = @(& git -C $repo status --short)
    phases = $phases
}
$manifest = Join-Path $run 'run.json'
function Save-Context {
    $context | ConvertTo-Json -Depth 8 | Set-Content -Encoding utf8 -LiteralPath $manifest
}
function Invoke-Phase([string]$Name, [string]$Program, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath (Join-Path $run "$Name.log")
        $nativeExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $phases.Add([ordered]@{ name = $Name; command = $Program; arguments = $Arguments; exit_code = $nativeExit })
    Save-Context
    if ($nativeExit -ne 0) { throw "$Name failed with exit code $nativeExit; see $run" }
}
$savedOutput = [Environment]::GetEnvironmentVariable('TESSERA_GPU_OUTPUT', 'Process')
Save-Context
try {
    Invoke-Phase 'configure' 'cmake' (@('-S', $repo, '-B', $build, '-A', 'x64',
        '-DBUILD_TESTING=ON', '-DTESSERA_BUILD_TESTING=ON', '-DTESSERA_BUILD_EXAMPLES=ON',
        '-DTESSERA_BUILD_VULKAN=ON', '-DTESSERA_BUILD_FONTS=ON') + $CMakeArguments)
    Copy-Item -LiteralPath (Join-Path $build 'CMakeCache.txt') -Destination (Join-Path $run 'CMakeCache.txt')
    $selection = '^(vulkan|vulkan_ui|vulkan_menu(_fonts)?_smoke)$'
    foreach ($config in $Configuration) {
        $output = Join-Path $run $config
        New-Item -ItemType Directory -Path $output -Force | Out-Null
        $env:TESSERA_GPU_OUTPUT = $output
        Invoke-Phase "build-$config" 'cmake' @('--build', $build, '--config', $config,
            '--parallel', "$BuildParallelism", '--target', 'tessera_vulkan_tests', 'tessera_vulkan_menu',
            '--', '/nodeReuse:false')
        $inventory = & ctest --test-dir $build -C $config -R $selection --show-only=json-v1
        if ($LASTEXITCODE -ne 0) { throw "CTest inventory failed for $config" }
        $inventory | Set-Content -Encoding utf8 -LiteralPath (Join-Path $output 'inventory.json')
        $tests = @((($inventory -join "`n") | ConvertFrom-Json).tests)
        $required = @('vulkan', 'vulkan_ui', 'vulkan_menu_smoke', 'vulkan_menu_fonts_smoke')
        $disabled = @($tests | Where-Object {
            @($_.properties | Where-Object { $_.name -eq 'DISABLED' -and $_.value }).Count -ne 0
        })
        if ($tests.Count -ne $required.Count -or
            @(Compare-Object @($tests.name | Sort-Object) @($required | Sort-Object)).Count -ne 0 -or
            $disabled.Count -ne 0) {
            throw "Expected all four enabled GPU/UI/native checks for $config; see $output"
        }
        Invoke-Phase "test-$config" 'ctest' @('--test-dir', $build, '-C', $config, '-R', $selection,
            '--output-on-failure', '--no-tests=error', '--stop-on-failure', '--timeout', '300', '-j', '1',
            '--output-junit', (Join-Path $output 'ctest.xml'),
            '--output-log', (Join-Path $output 'ctest.log'))
    }
    $context.status = 'passed'
} catch {
    $context.status = 'failed'
    $context['error'] = $_.Exception.Message
    throw
} finally {
    [Environment]::SetEnvironmentVariable('TESSERA_GPU_OUTPUT', $savedOutput, 'Process')
    $context['finished_utc'] = [datetime]::UtcNow.ToString('o')
    Save-Context
    Write-Output "GPU regression evidence: $run"
}

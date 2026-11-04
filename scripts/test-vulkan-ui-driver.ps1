[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$driver = Join-Path $PSScriptRoot 'test-vulkan-ui.ps1'
$scratch = [IO.Path]::GetFullPath((Join-Path ([IO.Path]::GetTempPath()) ("tessera-gpu-driver-" + [guid]::NewGuid().ToString('N'))))
New-Item -ItemType Directory -Path $scratch | Out-Null
$savedOutput = [Environment]::GetEnvironmentVariable('TESSERA_GPU_OUTPUT', 'Process')
$mockMode = ''
$mockTests = New-Object 'System.Collections.Generic.List[string]'
function cmake {
    $global:LASTEXITCODE = 0
    if ($args[0] -eq '-S') {
        $directory = $args[[array]::IndexOf($args, '-B') + 1]
        Set-Content -LiteralPath (Join-Path $directory 'CMakeCache.txt') -Value '# Mock configuration'
    }
    if ($args[0] -eq '--build' -and $mockMode -eq 'build-failure') { $global:LASTEXITCODE = 7 }
    Write-Output 'Mock cmake'
}
function ctest {
    $global:LASTEXITCODE = 0
    if ($args -contains '--show-only=json-v1') {
        if ($mockMode -eq 'inventory-failure') { $global:LASTEXITCODE = 9; return }
        $names = @('vulkan', 'vulkan_ui', 'vulkan_menu_smoke', 'vulkan_menu_fonts_smoke')
        if ($mockMode -eq 'missing') { $names = $names[0..2] }
        $tests = @($names | ForEach-Object {
            $properties = @()
            if ($mockMode -eq 'disabled' -and $_ -eq 'vulkan_ui') {
                $properties = @(@{ name = 'DISABLED'; value = $true })
            }
            @{ name = $_; properties = $properties }
        })
        @{ tests = $tests } | ConvertTo-Json -Depth 6
    } else {
        $config = $args[[array]::IndexOf($args, '-C') + 1]
        $mockTests.Add($config)
        if ($mockMode -eq 'test-failure') { $global:LASTEXITCODE = 8 }
        Write-Output 'Mock ctest'
    }
}
try {
    foreach ($mockMode in @('success', 'missing', 'disabled', 'build-failure', 'test-failure', 'inventory-failure')) {
        $mockTests.Clear()
        $env:TESSERA_GPU_OUTPUT = 'driver-environment-sentinel'
        $directory = Join-Path $scratch $mockMode
        $failed = $false
        $failureMessage = ''
        try { & $driver -BuildDirectory $directory | Out-Null }
        catch { $failed = $true; $failureMessage = $_.Exception.Message }
        $expectedSuccess = $mockMode -eq 'success'
        if ($failed -eq $expectedSuccess) { throw "Incorrect driver outcome: $mockMode ($failureMessage)" }
        if ($env:TESSERA_GPU_OUTPUT -ne 'driver-environment-sentinel') { throw "Driver leaked output environment: $mockMode" }
        $manifests = @(Get-ChildItem -LiteralPath $directory -Filter 'run.json' -Recurse -File)
        if ($manifests.Count -ne 1) { throw "Expected one persistent run manifest: $mockMode" }
        $manifest = Get-Content -Raw -LiteralPath $manifests[0].FullName | ConvertFrom-Json
        $expectedStatus = if ($expectedSuccess) { 'passed' } else { 'failed' }
        if ($manifest.status -ne $expectedStatus -or -not $manifest.finished_utc) {
            throw "Incorrect persisted status: $mockMode"
        }
        $expectedTests = if ($expectedSuccess) { 2 } elseif ($mockMode -eq 'test-failure') { 1 } else { 0 }
        if ($mockTests.Count -ne $expectedTests) { throw "Unexpected tests/retry after failure: $mockMode" }
        if ($expectedSuccess -and (($mockTests -join ',') -ne 'Debug,Release' -or $manifest.phases.Count -ne 5)) {
            throw 'The default driver must execute both configurations exactly once.'
        }
        if (-not $expectedSuccess -and -not $manifest.error) { throw "Failure lacks a diagnostic: $mockMode" }
    }
    foreach ($config in @(@(), @('Debug', 'Debug'))) {
        $failed = $false
        try { & $driver -BuildDirectory (Join-Path $scratch 'invalid') -Configuration $config | Out-Null }
        catch { $failed = $true }
        if (-not $failed) { throw 'Empty/duplicate configuration selection was accepted.' }
    }
    Write-Output 'Vulkan UI driver checks passed: six run outcomes, empty/duplicate configuration rejection and environment restoration.'
} finally {
    [Environment]::SetEnvironmentVariable('TESSERA_GPU_OUTPUT', $savedOutput, 'Process')
    Remove-Item -LiteralPath $scratch -Recurse -Force
}

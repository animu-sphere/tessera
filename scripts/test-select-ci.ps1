[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cases = @(
    @{ Paths = @(); Core = $true; Suite = 'full' },
    @{ Paths = @('docs/roadmap/current.md', 'README.md', 'formats/tessera-ui/README.md'); Core = $false; Suite = 'pr' },
    @{ Paths = @('examples/renderer-diagnostics/panel.cpp', 'docs/README.md'); Core = $true; Suite = 'pr' },
    @{ Paths = @('src/ui/document.cpp', 'tests/serialization/document_tests.cpp'); Core = $true; Suite = 'pr' },
    @{ Paths = @('CMakeLists.txt', 'docs/README.md'); Core = $true; Suite = 'full' },
    @{ Paths = @('tests/cmake/register.cmake'); Core = $true; Suite = 'full' },
    @{ Paths = @('include/tessera/ui/authoring.hpp'); Core = $true; Suite = 'full' },
    @{ Paths = @('.github/workflows/ci.yml'); Core = $true; Suite = 'full' },
    @{ Paths = @('scripts/select-ci.ps1'); Core = $true; Suite = 'full' },
    @{ Paths = @('backends/vulkan/vulkan.cpp'); Core = $true; Suite = 'full' },
    @{ Paths = @('unexpected/file'); Core = $true; Suite = 'full' },
    # Rename/deletion inventories include both paths; a code path cannot hide
    # behind its new documentation name. Unrecognized/case-varied names widen.
    @{ Paths = @('src/old.cpp', 'docs/new.md'); Core = $true; Suite = 'pr' },
    @{ Paths = @('DOCS/README.MD'); Core = $true; Suite = 'full' }
)
foreach ($case in $cases) {
    $actual = & (Join-Path $PSScriptRoot 'select-ci.ps1') -Paths $case.Paths
    if ($actual.RunCore -ne $case.Core -or $actual.Suite -ne $case.Suite) {
        throw "Incorrect CI selection for: $($case.Paths -join ', ')"
    }
}
Write-Output "CI selection checks passed: $($cases.Count) cases."

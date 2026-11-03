[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cases = @(
    @{ Paths = @(); Core = $true; Suite = 'wide' },
    @{ Paths = @('docs/roadmap/current.md', 'README.md', 'formats/tessera-ui/README.md'); Core = $false; Suite = 'pr' },
    @{ Paths = @('.github/PULL_REQUEST_TEMPLATE.md', '.github/ISSUE_TEMPLATE/bug_report.yml', 'scripts/check-docs.ps1', 'LICENSE', 'docs/images/panel.png'); Core = $false; Suite = 'pr' },
    @{ Paths = @('examples/settings-starter/main.cpp', 'docs/README.md'); Core = $true; Suite = 'pr' },
    @{ Paths = @('src/ui/document.cpp', 'tests/serialization/document_tests.cpp', 'tests/serialization/fixtures/button.json'); Core = $true; Suite = 'pr' },
    @{ Paths = @('include/tessera/ui/authoring.hpp'); Core = $true; Suite = 'pr' },
    @{ Paths = @('backends/vulkan/vulkan.cpp'); Core = $true; Suite = 'pr' },
    @{ Paths = @('examples/renderer-diagnostics/panel.cpp'); Core = $true; Suite = 'fixtures' },
    @{ Paths = @('tests/integrations/renderer_diagnostics_tests.cpp', 'src/ui/document.cpp'); Core = $true; Suite = 'fixtures' },
    @{ Paths = @('examples/settings/main.cpp'); Core = $true; Suite = 'fixtures' },
    @{ Paths = @('CMakeLists.txt', 'docs/README.md'); Core = $true; Suite = 'imports' },
    @{ Paths = @('tests/cmake/register.cmake'); Core = $true; Suite = 'imports' },
    @{ Paths = @('.github/workflows/ci.yml'); Core = $true; Suite = 'imports' },
    @{ Paths = @('scripts/select-ci.ps1'); Core = $true; Suite = 'imports' },
    @{ Paths = @('VERSION'); Core = $true; Suite = 'imports' },
    @{ Paths = @('unexpected/file'); Core = $true; Suite = 'imports' },
    @{ Paths = @('CMakeLists.txt', 'examples/settings/main.cpp'); Core = $true; Suite = 'wide' },
    # Rename/deletion inventories include both paths; a code path cannot hide
    # behind its new documentation name. Case-varied names widen.
    @{ Paths = @('src/old.cpp', 'docs/new.md'); Core = $true; Suite = 'pr' },
    @{ Paths = @('DOCS/README.MD'); Core = $true; Suite = 'imports' }
)
foreach ($case in $cases) {
    $actual = & (Join-Path $PSScriptRoot 'select-ci.ps1') -Paths $case.Paths
    if ($actual.RunCore -ne $case.Core -or $actual.Suite -ne $case.Suite) {
        throw "Incorrect CI selection for: $($case.Paths -join ', ') (got Core=$($actual.RunCore) Suite=$($actual.Suite))"
    }
}
Write-Output "CI selection checks passed: $($cases.Count) cases."

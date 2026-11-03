[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$cases = @(
    @{ Paths = @(); Core = $true; Import = $true },
    @{ Paths = @('docs/roadmap/current.md', 'README.md', 'formats/tessera-ui/README.md'); Core = $false; Import = $false },
    @{ Paths = @('.github/PULL_REQUEST_TEMPLATE.md', '.github/ISSUE_TEMPLATE/bug_report.yml', 'scripts/check-docs.ps1', 'LICENSE', 'docs/images/panel.png'); Core = $false; Import = $false },
    @{ Paths = @('examples/settings-starter/main.cpp', 'docs/README.md'); Core = $true; Import = $false },
    @{ Paths = @('src/ui/document.cpp', 'tests/serialization/document_tests.cpp', 'tests/serialization/fixtures/button.json'); Core = $true; Import = $false },
    @{ Paths = @('src/ui/detail.hpp', 'modules/fonts/shaper.cpp'); Core = $true; Import = $false },
    @{ Paths = @('include/tessera/ui/authoring.hpp'); Core = $true; Import = $true },
    @{ Paths = @('include/tessera/new_api.h', 'examples/settings/main.cpp'); Core = $true; Import = $true },
    @{ Paths = @('backends/vulkan/vulkan.cpp', 'examples/vulkan-menu/main.cpp'); Core = $true; Import = $false },
    @{ Paths = @('examples/renderer-diagnostics/panel.cpp'); Core = $true; Import = $false },
    @{ Paths = @('tests/integrations/renderer_diagnostics_tests.cpp', 'src/ui/document.cpp'); Core = $true; Import = $false },
    @{ Paths = @('examples/settings/main.cpp'); Core = $true; Import = $false },
    @{ Paths = @('examples/editor-prototype/preview.json'); Core = $true; Import = $false },
    @{ Paths = @('examples/settings-starter/native_win32.hpp'); Core = $true; Import = $false },
    @{ Paths = @('CMakeLists.txt', 'docs/README.md'); Core = $true; Import = $true },
    @{ Paths = @('CMakePresets.json'); Core = $true; Import = $true },
    @{ Paths = @('cmake/TesseraTesting.cmake', 'cmake/TesseraOptions.cmake'); Core = $true; Import = $true },
    @{ Paths = @('tests/cmake/register.cmake', 'tests/cmake/consumer/CMakeLists.txt'); Core = $true; Import = $true },
    @{ Paths = @('examples/first-app/CMakeLists.txt'); Core = $true; Import = $true },
    @{ Paths = @('.github/workflows/ci.yml'); Core = $true; Import = $true },
    @{ Paths = @('scripts/select-ci.ps1'); Core = $true; Import = $true },
    @{ Paths = @('VERSION'); Core = $true; Import = $true },
    @{ Paths = @('unexpected/file'); Core = $true; Import = $true },
    @{ Paths = @('examples/new-example/main.cpp'); Core = $true; Import = $false },
    @{ Paths = @('tests/integrations/new_fixture.cpp'); Core = $true; Import = $false },
    @{ Paths = @('CMakeLists.txt', 'examples/settings/main.cpp'); Core = $true; Import = $true },
    # Rename/deletion inventories include both paths; a code path cannot hide
    # behind its new documentation name. Case-varied names widen.
    @{ Paths = @('src/old.cpp', 'docs/new.md'); Core = $true; Import = $false },
    @{ Paths = @('DOCS/README.MD'); Core = $true; Import = $true }
)
foreach ($case in $cases) {
    $actual = & (Join-Path $PSScriptRoot 'select-ci.ps1') -Paths $case.Paths
    if ($actual.RunCore -ne $case.Core -or $actual.Suite -cne 'core' -or
        $actual.RunImport -ne $case.Import) {
        throw "Incorrect CI selection for: $($case.Paths -join ', ') (got Core=$($actual.RunCore) Suite=$($actual.Suite) Import=$($actual.RunImport))"
    }
}
Write-Output "CI selection checks passed: $($cases.Count) cases."

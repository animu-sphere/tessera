# Conservative PR selection. Unknown paths and missing diffs widen the suite.
# Suites: pr (contract+smoke), fixtures (+ long Diagnostics/Settings fixtures),
# imports (+ add_subdirectory imports), wide (both). Both imports and the full
# unfiltered suite run only in integration.
[CmdletBinding()]
param([AllowEmptyCollection()][string[]]$Paths = @())
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$docs = '^(?:.+\.md|docs/.+|\.github/ISSUE_TEMPLATE/.+|scripts/check-docs\.ps1|LICENSE)$'
$source = '^(?:(?:src|include|examples|backends|modules)/.+\.(?:cpp|hpp|json)|tests/(?!cmake/).+\.(?:cpp|hpp|json))$'
$fixture = '^(?:examples/(?:renderer-diagnostics|settings)/|tests/integrations/)'
$runCore = $Paths.Count -eq 0
$fixtures = $Paths.Count -eq 0
$imports = $Paths.Count -eq 0
foreach ($path in $Paths) {
    if ($path -cmatch $docs) { continue }
    $runCore = $true
    if ($path -cmatch $fixture) { $fixtures = $true }
    if ($path -cnotmatch $source) { $imports = $true }
}
$suite = if ($fixtures -and $imports) { 'wide' } elseif ($fixtures) { 'fixtures' } elseif ($imports) { 'imports' } else { 'pr' }
[pscustomobject]@{ RunCore = $runCore; Suite = $suite }

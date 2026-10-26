# Conservative PR selection. Missing/unknown paths use the full core suite.
[CmdletBinding()]
param([AllowEmptyCollection()][string[]]$Paths = @())
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$docs = '^(?:[^/]+\.md|(?:docs|formats)/.+\.md)$'
$source = '^(?:(?:src|examples)/.+\.(?:cpp|hpp)|tests/(?!cmake/).+\.cpp)$'
$runCore = $Paths.Count -eq 0
$suite = 'pr'
foreach ($path in $Paths) {
    if ($path -cmatch $docs) { continue }
    $runCore = $true
    if ($path -cnotmatch $source) { $suite = 'full' }
}
if ($Paths.Count -eq 0) { $suite = 'full' }
[pscustomobject]@{ RunCore = $runCore; Suite = $suite }

# PR builds never enable examples. API/build/unknown changes add only the
# minimal independent core consumer; example verification belongs to integration.
[CmdletBinding()]
param([AllowEmptyCollection()][string[]]$Paths = @())
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$docs = '^(?:.+\.md|docs/.+|\.github/ISSUE_TEMPLATE/.+|scripts/check-docs\.ps1|LICENSE)$'
$source = '^(?:(?:src|include|examples|backends|modules)/.+\.(?:cpp|hpp|json)|tests/(?!cmake/).+\.(?:cpp|hpp|json))$'
$runCore = $Paths.Count -eq 0
$runImport = $Paths.Count -eq 0
foreach ($path in $Paths) {
    if ($path -cmatch $docs) { continue }
    $runCore = $true
    if ($path -cmatch '^include/' -or $path -cnotmatch $source) {
        $runImport = $true
    }
}
[pscustomobject]@{
    RunCore = $runCore
    Suite = 'core'
    RunImport = $runImport
}

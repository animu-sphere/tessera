[CmdletBinding()]
param(
    [switch]$Check,
    [switch]$All,
    [string]$Base = 'HEAD'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
Push-Location $repo
try {
    if ($All) {
        $paths = @(git -c core.quotepath=false ls-files --cached)
    } else {
        $paths = @(git -c core.quotepath=false diff --name-only --no-renames --diff-filter=ACMR $Base --)
    }
    if ($LASTEXITCODE -ne 0) { throw 'Cannot determine tracked formatting targets' }
    $files = @($paths | Where-Object {
        $_ -match '\.(c|cc|cpp|cxx|h|hh|hpp|hxx|inl)$' -and
        $_ -notmatch '(^|/)(third[_-]?party|vendor|external|generated|_deps|build(?:[-_][^/]*)?)(/|$)' -and
        (Test-Path -LiteralPath $_ -PathType Leaf)
    } | Sort-Object -Unique)
    if ($files.Count -eq 0) {
        Write-Output 'No tracked C/C++ files selected.'
        return
    }
    $required = (Get-Content -LiteralPath (Join-Path $repo '.clang-format-version') -Raw).Trim()
    Get-Command clang-format -ErrorAction Stop | Out-Null
    $version = & clang-format --version
    if ($LASTEXITCODE -ne 0 -or $version -notmatch "clang-format version $([regex]::Escape($required))(?:\s|$)") {
        throw "Expected clang-format $required; found '$version'. Install with: python -m pip install clang-format==$required"
    }
    $arguments = @(if ($Check) { '--dry-run'; '--Werror' } else { '-i' })
    foreach ($file in $files) {
        & clang-format --style=file @arguments -- $file
        if ($LASTEXITCODE -ne 0) { throw "clang-format failed: $file" }
    }
    $operation = if ($Check) { 'Checked' } else { 'Formatted' }
    Write-Output "$operation $($files.Count) tracked C/C++ files with clang-format $required."
} finally {
    Pop-Location
}

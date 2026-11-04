[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$fixture = Join-Path ([IO.Path]::GetTempPath()) ('tessera-format-' + [guid]::NewGuid().ToString('N'))
$shell = (Get-Process -Id $PID).Path
$utf8 = New-Object Text.UTF8Encoding($false)
New-Item -ItemType Directory -Path (Join-Path $fixture 'scripts') -Force | Out-Null
function Write-Fixture([string]$Path, [string]$Content) {
    $target = Join-Path $fixture $Path
    New-Item -ItemType Directory -Path (Split-Path $target) -Force | Out-Null
    [IO.File]::WriteAllText($target, $Content, $utf8)
}
function Invoke-Check([string[]]$Arguments, [bool]$Success) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $output = & $shell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $fixture 'scripts\format-cpp.ps1') @Arguments 2>&1
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if (($code -eq 0) -ne $Success) {
        throw "Unexpected formatter exit $code for $Arguments`n$($output -join "`n")"
    }
}
Push-Location $fixture
try {
    Copy-Item -LiteralPath (Join-Path $repo 'scripts\format-cpp.ps1') -Destination 'scripts'
    foreach ($name in '.clang-format', '.clang-format-version', '.clang-format-ignore') {
        Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $fixture
    }
    git init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'Fixture git init failed' }
    git config core.autocrlf false
    if ($LASTEXITCODE -ne 0) { throw 'Fixture git configuration failed' }
    $bad = "int  main( ){return 0;}`n"
    $good = "int main() { return 0; }`n"
    Write-Fixture 'src\unchanged.cpp' $bad
    Write-Fixture 'src\changed file.cpp' $good
    Write-Fixture 'src\deleted.cpp' $bad
    Write-Fixture 'src\renamed.cpp' $good
    $excluded = @('Third_party\lib.cpp', 'nested\third-party\lib.hpp',
        'vendor\lib.h', 'external\lib.c', 'generated\lib.cpp',
        'build\lib.cpp', 'build-debug\lib.cpp', 'build_local\lib.cpp', '_deps\lib.cpp')
    git add .
    if ($LASTEXITCODE -ne 0) { throw 'Fixture git add failed' }
    git -c user.name=FormatterTests -c user.email=formatter@example.invalid -c commit.gpgsign=false commit --quiet -m 'Formatter fixture' -m 'Co-authored-by: Copilot <223556219+Copilot@users.noreply.github.com>'
    if ($LASTEXITCODE -ne 0) { throw 'Fixture git commit failed' }
    Invoke-Check @('-Check') $true
    foreach ($path in $excluded) { Write-Fixture $path $bad }
    Write-Fixture 'src\changed file.cpp' $bad
    Write-Fixture 'src\new.hpp' $bad
    Write-Fixture 'src\not-tracked.cpp' $bad
    Remove-Item -LiteralPath 'src\deleted.cpp'
    Move-Item -LiteralPath 'src\renamed.cpp' -Destination 'src\renamed file.cpp'
    git add .
    if ($LASTEXITCODE -ne 0) { throw 'Fixture staging failed' }
    git rm --cached --quiet -- 'src\not-tracked.cpp'
    if ($LASTEXITCODE -ne 0) { throw 'Fixture untracking failed' }
    Invoke-Check @('-Check') $false
    if ([IO.File]::ReadAllText((Join-Path $fixture 'src\changed file.cpp')) -cne $bad) {
        throw 'Check mode modified a file'
    }
    Invoke-Check @() $true
    Invoke-Check @('-Check') $true
    if ([IO.File]::ReadAllText((Join-Path $fixture 'src\changed file.cpp')) -cne $good) {
        throw 'Changed file was not formatted'
    }
    if ([IO.File]::ReadAllText((Join-Path $fixture 'src\unchanged.cpp')) -cne $bad) {
        throw 'Unchanged legacy file was formatted'
    }
    if ([IO.File]::ReadAllText((Join-Path $fixture 'src\not-tracked.cpp')) -cne $bad) {
        throw 'Untracked file was formatted'
    }
    Invoke-Check @('-Check', '-All') $false
    Invoke-Check @('-All') $true
    Invoke-Check @('-Check', '-All') $true
    foreach ($path in $excluded) {
        & clang-format --style=file -i -- $path
        if ($LASTEXITCODE -ne 0) { throw "Direct formatter invocation failed: $path" }
        if ([IO.File]::ReadAllText((Join-Path $fixture $path)) -cne $bad) {
            throw "Excluded file was formatted: $path"
        }
    }
    Write-Fixture '.clang-format-version' '0.0.0'
    Invoke-Check @('-Check', '-All') $false
    Invoke-Check @('-Check', '-Base', 'missing-revision') $false
    $global:LASTEXITCODE = 0 # Expected native failures above were asserted.
    Write-Output 'Formatter checks passed: changed/new/renamed/deleted/untracked files, exclusions, all-files mode, version and invalid base.'
} finally {
    Pop-Location
    Remove-Item -LiteralPath $fixture -Recurse -Force
}

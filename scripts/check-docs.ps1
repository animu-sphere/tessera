# Local Markdown structure and ownership checks; no runtime/toolchain dependencies.
[CmdletBinding()]
param()
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$issues = New-Object 'System.Collections.Generic.List[string]'
$documents = @(
    Get-ChildItem -LiteralPath $repoRoot -Filter '*.md' -File
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'docs') -Filter '*.md' -File -Recurse
    Get-ChildItem -LiteralPath (Join-Path $repoRoot 'formats') -Filter '*.md' -File -Recurse
)
$plain = @{}
$anchors = @{}
foreach ($file in $documents) {
    $relative = $file.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
    $visible = New-Object 'System.Collections.Generic.List[string]'
    $headings = New-Object 'System.Collections.Generic.HashSet[string]'
    $counts = @{}
    $fenceCharacter = ''
    $fenceLength = 0
    foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
        $fence = [regex]::Match($line, '^\s{0,3}(`{3,}|~{3,})(.*)$')
        if ($fence.Success) {
            $marker = $fence.Groups[1].Value
            if ($fenceLength -eq 0) {
                $fenceCharacter = $marker.Substring(0, 1)
                $fenceLength = $marker.Length
            } elseif ($marker.StartsWith($fenceCharacter) -and $marker.Length -ge $fenceLength -and
                      [string]::IsNullOrWhiteSpace($fence.Groups[2].Value)) {
                $fenceLength = 0
            }
            continue
        }
        if ($fenceLength -ne 0) { continue }
        $visible.Add($line)
        $heading = [regex]::Match($line, '^#{1,6}\s+(.+?)\s*#*\s*$')
        if ($heading.Success) {
            # GitHub-style fragments for the repository's ATX headings.
            $slug = $heading.Groups[1].Value.ToLowerInvariant()
            $slug = [regex]::Replace($slug, '[^\p{L}\p{M}\p{N}_\-\s]', '')
            $slug = [regex]::Replace($slug, '\s', '-')
            $base = $slug
            if ($counts.ContainsKey($base)) {
                $counts[$base]++
                $slug = $base + '-' + $counts[$base]
            } else { $counts[$base] = 0 }
            [void]$headings.Add($slug)
        }
    }
    if ($fenceLength -ne 0) { $issues.Add("${relative}: unclosed code fence") }
    $body = $visible -join "`n"
    $plain[$file.FullName] = $body
    $anchors[$file.FullName] = $headings
    if ($relative -ne 'docs/reference/support-matrix.md') {
        if ($body -match '(?m)^(Status|Updated|Checked):') {
            $issues.Add("${relative}: rolling status/date belongs in support or dated history")
        }
        if ($body -match '(?im)^\|.*\bCurrent (state|status)\b.*\|') {
            $issues.Add("${relative}: duplicate live status table")
        }
    }
    if ($relative -ne 'CHANGELOG.md' -and $relative -ne 'docs/reference/support-matrix.md' -and
        $body -match '(?im)^\s*[-*]\s+\[x\]') {
        $issues.Add("${relative}: delivered checklist belongs in changelog/support")
    }
    if ($relative -eq 'docs/roadmap/current.md' -and $body -match '(?im)^##\s+(Delivered|Phase [01]|Documentation foundation)') {
        $issues.Add("${relative}: current must contain remaining work, not foundation history")
    }
    # Contract and guide pages express maturity only through Implemented/Proposed labels.
    if ($relative -match '^(docs/design/|docs/guides/|formats/)' -or $relative -eq 'docs/reference/dependencies.md') {
        $progress = [regex]::Match($body, '(?i)\b(not yet|yet|currently|still needs?|remains? (a )?proposals?|remains? planned|(is|are) planned|no [a-z/ -]{1,40} (API|CLI|implementation) exists)\b')
        if ($progress.Success) {
            $issues.Add("${relative}: progress wording '$($progress.Value)' belongs in support; state the contract or link the proposal")
        }
    }
    if ($relative -match '^docs/design/' -and $body -match '(?i)\b(v\d+\.\d+(\.\d+)?|Phase \d+)\b') {
        $issues.Add("${relative}: version/milestone labels belong in current or backlog")
    }
    if ($relative -eq 'docs/reference/support-matrix.md' -and $body -match '(?im)^\|[^|\r\n]+\|\s*Planned\b') {
        $issues.Add("${relative}: capabilities without implementation have no row")
    }
}
$linkCount = 0
foreach ($file in $documents) {
    $relative = $file.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
    $body = [regex]::Replace($plain[$file.FullName], '`+[^`\r\n]*`+', '')
    $links = [regex]::Matches($body, '\[[^\]\r\n]*\]\((?<target><[^>]+>|[^\s)]+)(?:\s+"[^"]*")?\)')
    foreach ($link in $links) {
        $target = $link.Groups['target'].Value.Trim('<', '>')
        if ($target -match '^[a-zA-Z]:[\\/]') {
            $issues.Add("${relative}: nonportable local link '$target'")
            continue
        }
        if ($target -match '^[a-zA-Z][a-zA-Z0-9+.-]*:' -or $target.StartsWith('//')) { continue }
        $linkCount++
        $parts = $target -split '#', 2
        $path = [Uri]::UnescapeDataString($parts[0])
        if ([IO.Path]::IsPathRooted($path)) {
            $issues.Add("${relative}: nonportable local link '$target'")
            continue
        }
        if ($path -eq '') { $resolved = $file.FullName }
        else { $resolved = [IO.Path]::GetFullPath((Join-Path $file.DirectoryName $path)) }
        if (-not $resolved.StartsWith($repoRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            $issues.Add("${relative}: nonportable local link '$target'")
            continue
        }
        if (-not (Test-Path -LiteralPath $resolved)) {
            $issues.Add("${relative}: missing link '$target'")
            continue
        }
        if ($parts.Length -eq 2 -and $parts[1] -ne '') {
            $fragment = [Uri]::UnescapeDataString($parts[1])
            if (-not $anchors.ContainsKey($resolved) -or -not $anchors[$resolved].Contains($fragment)) {
                $issues.Add("${relative}: missing Markdown heading '$target'")
            }
        }
    }
}
$index = $plain[(Join-Path $repoRoot 'docs/README.md')]
foreach ($file in $documents) {
    if ($file.FullName.StartsWith((Join-Path $repoRoot 'docs') + [IO.Path]::DirectorySeparatorChar) -and
        $file.FullName -ne (Join-Path $repoRoot 'docs/README.md')) {
        $owner = $file.FullName.Substring((Join-Path $repoRoot 'docs').Length + 1).Replace('\', '/')
        if (-not $index.Contains('](' + $owner + ')')) { $issues.Add("docs/README.md: missing canonical owner '$owner'") }
    }
}
$requiredLinks = @{
    'README.md' = @('docs/README.md', 'docs/reference/support-matrix.md', 'docs/roadmap/current.md')
    'docs/roadmap/current.md' = @('../reference/support-matrix.md', '../../CHANGELOG.md', 'backlog.md')
    'docs/roadmap/backlog.md' = @('current.md', '../reference/support-matrix.md')
}
foreach ($relative in $requiredLinks.Keys) {
    $body = $plain[(Join-Path $repoRoot $relative)]
    foreach ($target in $requiredLinks[$relative]) {
        if (-not $body.Contains('](' + $target + ')')) { $issues.Add("${relative}: missing ownership link '$target'") }
    }
}
if ($issues.Count -gt 0) {
    foreach ($issue in $issues) { Write-Output $issue }
    Write-Output ("Documentation checks failed: {0} issue(s)." -f $issues.Count)
    exit 1
}
Write-Output ("Documentation checks passed: {0} Markdown files, {1} local links, ownership/status guards." -f $documents.Count, $linkCount)

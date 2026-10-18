# Runs every <name>.in against <name>.expected, preserving stdout bytes in <name>.actual.
# Sets ending in .raw use -raw; .each uses -each; other sets use one session.
# Usage: powershell -File tools/golden.ps1 [-Build DIR] [-Golden DIR] [-Dicts DIR]
param(
    [string]$Build,
    [string]$Golden = $env:HDTRANS_GOLDEN,
    [string]$Dicts = $env:HDTRANS_DICTS
)
$ErrorActionPreference = 'Stop'
$here = Split-Path $PSScriptRoot -Parent
if (-not $Build) { $Build = Join-Path $here 'build/msvc-x86' }
if (-not $Golden) { $Golden = Join-Path $here '../hdtrans-golden' }
if (-not $Dicts) { $Dicts = Join-Path $here '../dicts/base' }

try {
    $cli = Join-Path (Resolve-Path -LiteralPath $Build).Path 'hdtrans.exe'
    if (-not (Test-Path -LiteralPath $cli -PathType Leaf)) { throw "no CLI at $cli" }
    $Golden = (Resolve-Path -LiteralPath $Golden).Path
    $Dicts = (Resolve-Path -LiteralPath $Dicts).Path
    $inputs = @(Get-ChildItem -LiteralPath $Golden -Filter '*.in' -File | Sort-Object Name)
    if ($inputs.Count -eq 0) { throw "no sets in $Golden" }

    # One character per byte; ordinal comparisons retain CR, BOM and missing final LF.
    $encoding = [System.Text.Encoding]::GetEncoding(28591)
    $status = 0
    foreach ($inputFile in $inputs) {
        $name = $inputFile.BaseName
        $expected = Join-Path $Golden "$name.expected"
        $actual = Join-Path $Golden "$name.actual"
        $flag = if ($name.EndsWith('.raw')) { ' -raw' } elseif ($name.EndsWith('.each')) { ' -each' } else { '' }
        if (-not (Test-Path -LiteralPath $expected -PathType Leaf)) { throw "no expectation at $expected" }

        $process = New-Object System.Diagnostics.Process
        $process.StartInfo.FileName = $cli
        $process.StartInfo.UseShellExecute = $false
        $process.StartInfo.RedirectStandardOutput = $true
        # Double trailing backslashes before the closing argument quote.
        $process.StartInfo.Arguments = '-reference -dicts "{0}"{1} -in "{2}"' -f ($Dicts -replace '(\\+)$', '$1$1'), $flag, $inputFile.FullName
        $output = [System.IO.File]::Create($actual)
        try {
            [void]$process.Start()
            $process.StandardOutput.BaseStream.CopyTo($output)
            $process.WaitForExit()
            if ($process.ExitCode -ne 0) {
                Write-Host "$name`: CLI exit=$($process.ExitCode)"
                $status = 1
            }
        } finally {
            $output.Dispose()
            $process.Dispose()
        }

        $expectedLines = [regex]::Matches($encoding.GetString([System.IO.File]::ReadAllBytes($expected)), '[^\n]*\n|[^\n]+\z')
        $actualLines = [regex]::Matches($encoding.GetString([System.IO.File]::ReadAllBytes($actual)), '[^\n]*\n|[^\n]+\z')
        $total = $expectedLines.Count
        $different = 0
        for ($i = 0; $i -lt [Math]::Max($expectedLines.Count, $actualLines.Count); $i++) {
            if ($i -ge $expectedLines.Count -or $i -ge $actualLines.Count -or
                -not [string]::Equals($expectedLines[$i].Value, $actualLines[$i].Value, [System.StringComparison]::Ordinal)) {
                $different++
            }
        }
        Write-Host "$name`: $different differing lines of $total"
        if ($different -ne 0) { $status = 1 }
    }
    exit $status
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 2
}

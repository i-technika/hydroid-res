# Fails unless hydroid-rc ends with an error, not an abort, when an argument is UTF-16 that is not
# Unicode text: a lone surrogate, which wmain takes as it is and which has no UTF-8 (#87). Windows
# only; CMake, which passes arguments through UTF-8, cannot run hydroid-rc so. -Rc is hydroid-rc,
# from tests/CMakeLists.txt.
param([Parameter(Mandatory = $true)][string]$Rc)

$lone = [string][char]0xD800
$missing = Join-Path $env:TEMP 'hydroid-rc-no-such-folder'
# Arguments, and the exit code: 1 as for a folder that fails to load, 2 for usage.
$cases = @(
  @{ Args = @('dump', "$missing$lone"); Exit = 1 },
  @{ Args = @('dump', $missing, "com.a$lone"); Exit = 1 },
  @{ Args = @("dump$lone", $missing); Exit = 2 },
  @{ Args = @("--version$lone"); Exit = 2 }
)
$failed = $false
foreach ($case in $cases) {
  $a = $case.Args
  & $Rc @a 2>$null | Out-Null
  $shown = ($a | ForEach-Object { $_.Replace($lone, '<U+D800>') }) -join ' '
  if ($LASTEXITCODE -ne $case.Exit) {
    Write-Output ("hydroid-rc {0}: exit {1} (0x{1:x8}), not {2}" -f $shown, $LASTEXITCODE, $case.Exit)
    $failed = $true
  } else {
    Write-Output ("hydroid-rc {0}: exit {1}" -f $shown, $LASTEXITCODE)
  }
}
if ($failed) { exit 1 }

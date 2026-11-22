# Fails unless hydroid-rc ends with an error that names the cause, not an abort, when an argument
# or a name in the folder is UTF-16 that is not Unicode text: a lone surrogate, which wmain takes
# as it is and which has no UTF-8 (#87). Windows only; CMake, which passes arguments through UTF-8,
# cannot run hydroid-rc so. -Rc is hydroid-rc, from tests/CMakeLists.txt.
param([Parameter(Mandatory = $true)][string]$Rc)

$lone = [string][char]0xD800
$missing = Join-Path $env:TEMP 'hydroid-rc-no-such-folder'
# A res/ folder whose arguments are all Unicode, with a file whose name is not.
$res = Join-Path $env:TEMP "hydroid-rc-not-unicode-$PID\res"
New-Item -ItemType Directory -Force (Join-Path $res 'values') | Out-Null
[System.IO.File]::WriteAllText((Join-Path $res "values\b$lone.xml"), '<resources/>')

# Arguments, the exit code (1 as for a folder that fails to load, 2 for usage) and what stderr
# must say: for a path, the library's diagnostic, which writes it with U+FFFD (#111), and of which
# only ASCII words are checked, as PowerShell reads stderr in the console's code page, where the
# bytes of U+FFFD may run into what follows.
$cases = @(
  @{ Args = @('dump', "$missing$lone"); Exit = 1; Says = 'not a folder' },
  @{ Args = @('dump', $missing, "com.a$lone"); Exit = 1;
     Says = 'error: the package name is not Unicode text' },
  @{ Args = @('dump', $res); Exit = 1; Says = 'file not found' },
  @{ Args = @("dump$lone", $missing); Exit = 2; Says = 'usage:' },
  @{ Args = @('pick', "$missing$lone", 'land'); Exit = 1; Says = 'not a folder' },
  @{ Args = @('pick', $missing, "land$lone"); Exit = 1;
     Says = 'error: an argument is not Unicode text' },
  @{ Args = @('pick', $missing, 'land', '--locales', "ko$lone"); Exit = 1;
     Says = 'error: an argument is not Unicode text' },
  @{ Args = @('pick', $missing, 'land', "com.a$lone"); Exit = 1;
     Says = 'error: an argument is not Unicode text' },
  # The file of --locale-config is a path, as the folder is, and not text (#268).
  @{ Args = @('pick', $missing, 'land', '--locale-config', "$missing$lone"); Exit = 1;
     Says = 'failed to open file' },
  @{ Args = @("--version$lone"); Exit = 2; Says = 'usage:' }
)
$failed = $false
foreach ($case in $cases) {
  $a = $case.Args
  $err = (& $Rc @a 2>&1 | Where-Object { $_ -is [System.Management.Automation.ErrorRecord] } |
          ForEach-Object { $_.ToString() }) -join "`n"
  $code = $LASTEXITCODE
  $shown = ($a | ForEach-Object { $_.Replace($lone, '<U+D800>') }) -join ' '
  if ($code -ne $case.Exit -or -not $err.Contains($case.Says)) {
    Write-Output ("hydroid-rc {0}: exit {1} (0x{1:x8}), not {2}; stderr:`n{3}" -f
                  $shown, $code, $case.Exit, $err)
    $failed = $true
  } else {
    Write-Output ("hydroid-rc {0}: exit {1}, {2}" -f $shown, $code, $case.Says)
  }
}
Remove-Item -Recurse -Force (Split-Path $res)
if ($failed) { exit 1 }

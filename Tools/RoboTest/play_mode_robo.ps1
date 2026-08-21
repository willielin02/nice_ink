# Robo test mode: restore the harness configuration -
# editor opens L_Dojo, Play = 3 clients listen server in ONE process
# (the tick-harness python drives PIE worlds in-process; separate processes
# would be unreachable). Run with the EDITOR CLOSED.
# ASCII only (PS 5.1 mangles non-ASCII .ps1 without BOM).
$ErrorActionPreference = "Stop"
if (Get-Process UnrealEditor* -ErrorAction SilentlyContinue) {
    throw "Close the editor first - it overwrites play settings on exit."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)

# 1) Editor startup map -> dojo (robo scripts begin_play on the startup map)
$engIni = "c:\games\Unreal Engine\nice_ink\Config\DefaultEngine.ini"
$t = [System.IO.File]::ReadAllText($engIni, $utf8)
$t = $t -replace "EditorStartupMap=/Game/Maps/L_MainMenu\.L_MainMenu", "EditorStartupMap=/Game/Maps/L_Dojo.L_Dojo"
[System.IO.File]::WriteAllText($engIni, $t, $utf8)

# 2) Play settings -> 3 clients, listen server, one process
$usrIni = "c:\games\Unreal Engine\nice_ink\Saved\Config\WindowsEditor\EditorPerProjectUserSettings.ini"
$u = [System.IO.File]::ReadAllText($usrIni, $utf8)
$u = $u -replace "PlayNetMode=PIE_\w+", "PlayNetMode=PIE_ListenServer"
$u = $u -replace "RunUnderOneProcess=\w+", "RunUnderOneProcess=True"
$u = $u -replace "PlayNumberOfClients=\d+", "PlayNumberOfClients=2"
[System.IO.File]::WriteAllText($usrIni, $u, $utf8)

Write-Host "ROBO MODE: editor opens L_Dojo, Play = 2-client listen PIE in one process."
Write-Host "Remember the StartupScripts ini line workflow from README.md."

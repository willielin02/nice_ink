# Party play mode: editor Play = 4 separate-process standalone windows starting
# at the MAIN MENU (real ServerTravel + real LAN discovery -> full happy path:
# menu -> host/join -> lobby -> ENTER -> match).
# Run with the EDITOR CLOSED (it rewrites EditorPerProjectUserSettings on exit).
# ASCII only (PS 5.1 mangles non-ASCII .ps1 without BOM).
$ErrorActionPreference = "Stop"
if (Get-Process UnrealEditor* -ErrorAction SilentlyContinue) {
    throw "Close the editor first - it overwrites play settings on exit."
}

$utf8 = New-Object System.Text.UTF8Encoding($false)

# 1) Editor startup map -> main menu
$engIni = "c:\games\Unreal Engine\nice_ink\Config\DefaultEngine.ini"
$t = [System.IO.File]::ReadAllText($engIni, $utf8)
$t = $t -replace "EditorStartupMap=/Game/Maps/L_Dojo\.L_Dojo", "EditorStartupMap=/Game/Maps/L_MainMenu.L_MainMenu"
[System.IO.File]::WriteAllText($engIni, $t, $utf8)

# 2) Play settings -> 4 players, standalone, separate processes, 720p windows
$usrIni = "c:\games\Unreal Engine\nice_ink\Saved\Config\WindowsEditor\EditorPerProjectUserSettings.ini"
$u = [System.IO.File]::ReadAllText($usrIni, $utf8)
$u = $u -replace "PlayNetMode=PIE_\w+", "PlayNetMode=PIE_Standalone"
$u = $u -replace "RunUnderOneProcess=\w+", "RunUnderOneProcess=False"
$u = $u -replace "PlayNumberOfClients=\d+", "PlayNumberOfClients=4"
$u = $u -replace "ClientWindowWidth=\d+", "ClientWindowWidth=1280"
$u = $u -replace "ClientWindowHeight=\d+", "ClientWindowHeight=720"
[System.IO.File]::WriteAllText($usrIni, $u, $utf8)

Write-Host "PARTY MODE: Play = 4 standalone windows from the main menu."
Write-Host "Host in one window, join in the others, ENTER in lobby to start."

# Record a running game window to mp4 with ffmpeg (gdigrab) - no input injection,
# no focus stealing. Used for the UI flow video (2026-09-05 neutral UI acceptance:
# "judge the flow on a video, not on a screenshot").
#   .\record_window.ps1 -Title "NiceInk" -Seconds 60 -Out "Saved\UiMock\flow.mp4"
# The title must match the window title exactly as gdigrab sees it (Get-Process | ? MainWindowTitle).
# ASCII only (PS 5.1 mangles non-ASCII .ps1 without BOM).
param(
    [string]$Title = "NiceInk",
    [int]$Seconds = 60,
    [string]$Out = "c:\games\Unreal Engine\nice_ink\Saved\UiMock\flow.mp4",
    [int]$Fps = 30
)
$ErrorActionPreference = "Stop"
$ff = (Get-Command ffmpeg -ErrorAction Stop).Source
$dir = Split-Path $Out
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
# exact match first (titles contain [brackets] which -like treats as a char class), then substring
$win = Get-Process | Where-Object { $_.MainWindowTitle -eq $Title } | Select-Object -First 1
if (-not $win) { $win = Get-Process | Where-Object { $_.MainWindowTitle -and $_.MainWindowTitle.Contains($Title) } | Select-Object -First 1 }
if (-not $win) { throw "no window with title like *$Title*" }
$t = $win.MainWindowTitle
Write-Host "recording '$t' for $Seconds s -> $Out"
& $ff -y -loglevel error -f gdigrab -framerate $Fps -i ("title=" + $t) -t $Seconds -c:v libx264 -preset veryfast -crf 20 -pix_fmt yuv420p $Out
Write-Host "done: $Out"

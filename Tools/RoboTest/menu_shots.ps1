# 主選單六頁的實機截圖（2026-09-05；全站 UI 對齊 Meccha 的驗收證據）。
# 局內有 robo harness 可以一次跑完，選單沒有——ExecCmds 的指令是**同時**執行的，
# 換頁與拍照不能排在同一次啟動裡（換頁會立刻生效，晚拍的那張就拍到別頁）。
# 所以一頁一次啟動，每次 -game 開主選單 → 換頁 → 延遲拍照 → 自己關掉。
#
# 截圖走 `NiMenuShot`（PC->ConsoleCommand("Shot showui")）——**HighResShot 不含
# Slate UI**，選單整個是 Slate，用錯就拍到一片黑底加一隻力士。
# ASCII only（PS 5.1 對無 BOM 的非 ASCII .ps1 會亂碼）。
$ErrorActionPreference = "Stop"
$UE = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
$PROJ = "c:\games\Unreal Engine\nice_ink\NiceInk.uproject"
$SHOTDIR = "c:\games\Unreal Engine\nice_ink\Saved\Screenshots\WindowsEditor"

# LAN override: same as play_full_flow.bat (EOS ini + bare -game = network error)
$NETARG = '-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL'

# page -> the exec command that navigates there ("" = root, already there)
$pages = @(
    @{ name = "menu2_root";     nav = "" },
    @{ name = "menu2_host";     nav = "NiMenuShowHost" },
    @{ name = "menu2_join";     nav = "NiMenuShowJoin" },
    @{ name = "menu2_settings"; nav = "NiMenuShowSettings" },
    @{ name = "menu2_language"; nav = "NiMenuShowLang" },
    @{ name = "menu2_profile";  nav = "NiMenuShowProfile" }
)

foreach ($p in $pages) {
    $cmds = @()
    if ($p.nav -ne "") { $cmds += $p.nav }
    $cmds += "NiMenuShot 8 $($p.name)"
    # TRAP (CLAUDE.md): a uproject path with spaces must carry its OWN quotes, and
    # -ArgumentList as an ARRAY does not survive here - UE receives two fragments,
    # loads nothing, opens the Project Browser and writes its log to Engine/Saved/Logs
    # (symptom: editor is up, CPU low, project log never moves). Build ONE string.
    $argline = ('"{0}" -game -windowed -resx=1920 -resy=1080 -WinX=0 -WinY=0 {1} -ExecCmds="{2}"' `
        -f $PROJ, $NETARG, ($cmds -join ","))
    Write-Host "--- $($p.name)"
    $proc = Start-Process -FilePath $UE -ArgumentList $argline -PassThru
    Start-Sleep -Seconds 50
    Stop-Process -Id $proc.Id -Force -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 4
}

Write-Host "=== shots on disk ==="
Get-ChildItem "$SHOTDIR\menu2_*" -ErrorAction SilentlyContinue |
    Select-Object Name, @{n = "KB"; e = { [math]::Round($_.Length / 1KB) } }

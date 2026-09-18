#!/bin/bash
# 四人真局 ESC 首頁自駕截圖（2026-09-18）：主機 AutoHost → 讀 log 拿房碼 → 三個沙箱以房碼加入
# → 主機 NiSysMenuOpen → NiShot 拍 ESC 首頁（含房碼＋四張臉＋三顆靴子）。零輸入注入。
set -u
cd "c:/games/Unreal Engine/nice_ink"
UE="C:/Program Files/Epic Games/UE_5.7/Engine/Binaries/Win64/UnrealEditor.exe"
PROJ='c:\games\Unreal Engine\nice_ink\NiceInk.uproject'
TAG="${1:-esc4}"
rm -f Saved/Screenshots/WindowsEditor/${TAG}_*.png
: > Saved/Logs/NiceInk.log 2>/dev/null

"$UE" "$PROJ" -game -windowed -resx=2560 -resy=1380 -WinX=0 -WinY=0 \
  -ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL -nilanloopback \
  -ExecCmds="t.MaxFPS 60,NiMenuAutoHost 0,NiDelayExec 92 NiSysMenuOpen,NiShot 97 ${TAG}_root,NiDelayExec 100 NiSysMenuClose" >/dev/null 2>&1 &
HOST=$!
CODE=""
for i in $(seq 1 40); do
  sleep 3
  CODE=$(grep -a -o "NiSession: room code [A-Z]*" Saved/Logs/NiceInk.log 2>/dev/null | tail -1 | awk '{print $4}')
  [ -n "$CODE" ] && break
done
echo "ROOM CODE: [$CODE]"
if [ -z "$CODE" ]; then echo "NO CODE"; kill $HOST; exit 1; fi

SUF=("P2" "P3" "P4")
for k in 0 1 2; do
  "$UE" "$PROJ" -game -windowed -resx=960 -resy=540 -WinX=$((1000 + k * 40)) -WinY=$((600 + k * 40)) \
    -ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL -nilanloopback -saveddirsuffix=${SUF[$k]} \
    -ExecCmds="t.MaxFPS 60,NiMenuJoinCode $CODE" >/dev/null 2>&1 &
  sleep 12
done

for i in $(seq 1 40); do
  [ -f Saved/Screenshots/WindowsEditor/${TAG}_root00000.png ] && break
  sleep 5
done
sleep 3
ls -l Saved/Screenshots/WindowsEditor/${TAG}_* 2>/dev/null
powershell -NoProfile -Command "Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force"
echo ESC4 DONE

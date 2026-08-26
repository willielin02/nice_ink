@echo off
rem FULL FLOW test, 4 DISTINCT PLAYERS: same as play_full_flow.bat, but windows
rem 2-4 get their own saved dir via -saveddirsuffix (engine-native): Saved_P2/
rem Saved_P3/ Saved_P4/ next to Saved/. Each instance has its own
rem NiceInk_Settings.sav (name/language/avatar), PlayerFace/ (uploaded selfie
rem + face library) and NiceInk_*_Seat*.sav (cash/tattoo persistence).
rem Window 1 uses the default Saved/ = your main profile.
rem First launch of a fresh suffix = brand-new player (random rikishiNN name,
rem no custom face). To reset one player, delete its Saved_PN folder.
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set PROJ="%~dp0..\NiceInk.uproject"
rem LAN override: ini is EOS now, but same-machine multi-instance shares one Epic
rem account (cannot join the same lobby) - local playtests must run LAN.
rem -nilanloopback: same-machine LAN beacon replies are eaten by the host's own
rem socket (Windows delivers shared-port unicast to the first binder; measured
rem 2026-08-13) - join-by-code falls back to 127.0.0.1 direct connect with the
rem code validated by host PreLogin. Two real machines don't need this flag.
set NETARG=-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL -nilanloopback
rem --- 多開節流（2026-08-25 效能調查）---
rem 一個視窗不設上限就會跑到 350~500fps（空道場 GPU 成本只有 1.05ms/幀），
rem 四開＝GPU 與 20 執行緒 CPU 同時被打爆，幀率在 390 與 11 之間亂跳，
rem 而且四個未 cook 的編輯器行程 × ~5GB 會把 32GB 記憶體吃到只剩 1.9GB
rem ＝D3D12 配置失敗，崩潰視窗寫「Out of video memory」但顯存其實還剩九成。
rem 開發多開一律 60 上限；正式版的上限在設定頁（預設 120）。
set FPSCAP=-ExecCmds="t.MaxFPS 60"
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40  %NETARG% %FPSCAP%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40  %NETARG% -saveddirsuffix=P2 %FPSCAP%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620 %NETARG% -saveddirsuffix=P3 %FPSCAP%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620 %NETARG% -saveddirsuffix=P4 %FPSCAP%

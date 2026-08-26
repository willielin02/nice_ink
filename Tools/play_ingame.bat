@echo off
rem IN-GAME ONLY test: skips menu/matchmaking entirely.
rem Window 1 boots straight into the dojo as listen server;
rem windows 2-4 direct-connect to it. Press ENTER in window 1's lobby to start.
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set PROJ="%~dp0..\NiceInk.uproject"
rem NULL override: ini defaults to EOS; bare ?listen / raw-IP connect need the
rem plain Ip net driver (EOS driver requires a session and errors back to menu).
set NETARG=-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL
rem --- 多開節流（2026-08-25 效能調查）---
rem 一個視窗不設上限就會跑到 350~500fps（空道場 GPU 成本只有 1.05ms/幀），
rem 四開＝GPU 與 20 執行緒 CPU 同時被打爆，幀率在 390 與 11 之間亂跳，
rem 而且四個未 cook 的編輯器行程 × ~5GB 會把 32GB 記憶體吃到只剩 1.9GB
rem ＝D3D12 配置失敗，崩潰視窗寫「Out of video memory」但顯存其實還剩九成。
rem 開發多開一律 60 上限；正式版的上限在設定頁（預設 120）。
set FPSCAP=-ExecCmds="t.MaxFPS 60"
start "" %UE% %PROJ% /Game/Maps/L_Dojo?listen -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40  %NETARG% %FPSCAP%
rem give the listen server time to bind before the clients connect
timeout /t 12 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40  %NETARG% %FPSCAP%
rem stagger the remaining clients so they don't all hammer load at once
timeout /t 4 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620 %NETARG% %FPSCAP%
timeout /t 4 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620 %NETARG% %FPSCAP%

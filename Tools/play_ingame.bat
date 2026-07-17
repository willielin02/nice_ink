@echo off
rem IN-GAME ONLY test: skips menu/matchmaking entirely.
rem Window 1 boots straight into the dojo as listen server;
rem windows 2-4 direct-connect to it. Press ENTER in window 1's lobby to start.
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set PROJ="%~dp0..\NiceInk.uproject"
start "" %UE% %PROJ% /Game/Maps/L_Dojo?listen -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40
rem give the listen server time to bind before the clients connect
timeout /t 12 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40
rem stagger the remaining clients so they don't all hammer load at once
timeout /t 4 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620
timeout /t 4 /nobreak >nul
start "" %UE% %PROJ% 127.0.0.1 -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620

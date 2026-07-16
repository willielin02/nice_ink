@echo off
rem FULL FLOW test: 4 standalone windows, each starting at the MAIN MENU.
rem Host in one window (host a room), join in the others (join a room),
rem then ENTER in the lobby to start. Real ServerTravel + real LAN discovery.
rem Uses editor binaries with uncooked assets: recompile C++ and rerun, no packaging.
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set PROJ="%~dp0..\NiceInk.uproject"
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620

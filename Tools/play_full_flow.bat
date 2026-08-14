@echo off
rem FULL FLOW test: 4 standalone windows, each starting at the MAIN MENU.
rem Host in one window (host a room), join in the others (join a room),
rem then ENTER in the lobby to start. Real ServerTravel + real LAN discovery.
rem Uses editor binaries with uncooked assets: recompile C++ and rerun, no packaging.
set UE="C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe"
set PROJ="%~dp0..\NiceInk.uproject"
rem LAN override: ini is EOS now, but same-machine multi-instance shares one Epic
rem account (cannot join the same lobby) - local playtests must run LAN.
rem -nilanloopback: same-machine LAN beacon replies are eaten by the host's own
rem socket (Windows delivers shared-port unicast to the first binder; measured
rem 2026-08-13) - join-by-code falls back to 127.0.0.1 direct connect with the
rem code validated by host PreLogin. Two real machines don't need this flag.
set NETARG=-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL -nilanloopback
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40  %NETARG%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40  %NETARG%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620 %NETARG%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620 %NETARG%

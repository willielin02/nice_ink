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
set NETARG=-ini:Engine:[OnlineSubsystem]:DefaultPlatformService=NULL
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=40  %NETARG%
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=40  %NETARG% -saveddirsuffix=P2
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=20   -WinY=620 %NETARG% -saveddirsuffix=P3
start "" %UE% %PROJ% -game -windowed -resx=960 -resy=540 -WinX=1000 -WinY=620 %NETARG% -saveddirsuffix=P4

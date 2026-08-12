@echo off
rem 全新玩家試玩（2026-08-12）：獨立 Saved_FRESH 沙箱＋每次啟動前清空
rem ＝每一次都是首啟體驗（無臉→創角頁、無本機設定）。
rem 實測修正：Epic 登入快取存在系統層（不在 Saved 裡）——沙箱照樣自動
rem 靜默登入、雲端 persona（名字/偏好/現金）跟著帳號回來；臉不在雲端
rem ＝創角流程照樣觸發。要連帳號都全新＝先登出 Epic 或用別的帳號。
rmdir /s /q "%~dp0..\Saved_FRESH" 2>nul
start "" "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor.exe" "%~dp0..\NiceInk.uproject" -game -windowed -resx=2560 -resy=1380 -saveddirsuffix=FRESH

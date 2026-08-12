@echo off
rem 重置 play_full_flow_4p 的 2-4 號玩家沙箱（2026-08-12）：
rem 刪 Saved_P2/P3/P4 ＝這三個視窗下次啟動回到全新玩家（創角頁）。
rem 視窗 1 用正本 Saved/（你的主檔案）——本腳本永不碰它。
rmdir /s /q "%~dp0..\Saved_P2" 2>nul
rmdir /s /q "%~dp0..\Saved_P3" 2>nul
rmdir /s /q "%~dp0..\Saved_P4" 2>nul
echo Saved_P2/P3/P4 已清空 - 下次 play_full_flow_4p 的 2-4 號視窗＝全新玩家。
pause

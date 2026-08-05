# SunoBgm — 全遊戲唯一 BGM 源檔

- `the_sneaky_koto_original.wav`：Suno（suno.com）生成曲「The Sneaky Koto」原檔
  （48kHz/16-bit 立體聲、1:16.96；風格 tag：japanese/sanshin/playful、instrumental）。
- `the_sneaky_koto_075.wav`：rubberband 0.75× 時間拉伸（音高不變）；1:42.60、-15.9 LUFS。
- `bgm_sneaky_koto.wav`：**進引擎的最終檔**（/Game/Audio/bgm_sneaky_koto，looping=True）。
  無縫 loop 加工＝尾 2s 淡出疊進頭 2s 淡入（等功率 qsin 交叉淡接、amix normalize=0）
  → 長度 100.60s、峰值 -1.2dB 無削波、接縫兩端能量連續。

再生指令（ffmpeg 8.x full build，含 librubberband）：

```powershell
ffmpeg -i the_sneaky_koto_original.wav -filter:a "rubberband=tempo=0.75" -c:a pcm_s16le the_sneaky_koto_075.wav
# loop 加工：L=102.60、X=2.0（尾頭等功率交叉淡接，見 git 史此檔案的提交訊息）
```

**授權備忘（出貨前必辦）**：Suno 生成曲的商用權跟付費方案走（免費層＝非商用）。
這首會進上架遊戲與行銷 clip——出貨前確認生成當時帳號方案含商用授權，並記入
credits 名單（SHIP_PLAN C8）。

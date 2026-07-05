#!/bin/bash
# 六位玩家臉貼圖批次生成：每人跑一次管線，把 out/ 的關鍵輸出歸檔到 out/players/<key>/
cd "c:/games/Unreal Engine/nice_ink/Tools/FacePipeline"
PY="c:/games/Unreal Engine/nice_ink_face_pipeline/venv/Scripts/python.exe"

declare -A PLAYERS=(
  [7AF4]="7AF4F8FF-4E03-4BE1-9F24-C89180F884FE.jpg"
  [cvd]="cVDcraFWdBNoAGZYYWHqmd (1).jpg"
  [caseoh]="d74dbdfef4c65c8271c42b882054311d.jpg"
  [ibai]="Ibai_Llanos_(2024)1.jpg"
  [img1]="images (1).jpg"
  [img0]="images.jpg"
)

for key in 7AF4 cvd caseoh ibai img1 img0; do
  echo "=== [$key] ${PLAYERS[$key]} ==="
  "$PY" selfie_to_face_texture.py "test_selfies/${PLAYERS[$key]}" || { echo "FAILED: $key"; continue; }
  mkdir -p "out/players/$key"
  cp out/face_texture.png "out/players/$key/face_texture.png"
  cp out/face_texture_eyes_closed.png "out/players/$key/face_texture_eyes_closed.png"
  cp out/eye_mask.png "out/players/$key/eye_mask.png"
  "$PY" bake_eye_ink_mask.py "out/players/$key/eye_mask.png" "out/players/$key/eye_mask_ink.png"
  cp out/skin_color.json "out/players/$key/skin_color.json"
  [ -f out/face_flags.json ] && cp out/face_flags.json "out/players/$key/face_flags.json"
  echo "=== [$key] done ==="
done
echo "ALL SIX DONE"

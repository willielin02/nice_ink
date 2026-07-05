# AssetPrep — 場景與角色資產處理腳本

> Blender: "C:\Program Files\Blender Foundation\Blender 5.1\blender.exe"

## build_sauna_fbx.py — 桑拿房修復
```
blender --background --python build_sauna_fbx.py
```
讀 `SourceAssets/Sauna_localyany/sauna_2k.glb`（CC BY, 見 ATTRIBUTION.txt）：
- 補第四面牆（複製 BackWall 沿房間中心鏡像 -> FrontWall_repair）
- 抽出全部貼圖成 PNG（materials -> textures 對照會印在 stdout）
- 匯出純幾何 `sauna_room_repaired.fbx`（**絕不 embed 貼圖**——UE5.7 Interchange 會斷言崩潰）

## build_char17_fbx.py — 角色靜態網格匯出
```
blender --background "Content/玩家/nice_ink_player_character17.blend" --python build_char17_fbx.py
```
- 骨架世界變換烘進網格資料（烘完即站立，**不要再自行旋轉**；方向由 FaceMask 臉部質心自動驗證）
- 材質槽合併到 slot 0；身體/馬賽克分開匯出（**單一 section**——多 section 曾觸發 FaceIndex/UV 亂碼）
- UV 順序 [0]=UVMap（繪畫圖集）[1]=FaceUV；FaceMask 匯為頂點色

## ue_import_assets.py — UE 匯入（參考）
匯入需在**開著的編輯器**內執行（commandlet 無 Slate 會斷言）。安全模式：
`register_slate_post_tick_callback` 一次性回呼中呼叫 `import_asset_tasks`。
FBX 匯入要點：combine_meshes、generate_lightmap_u_vs=False、
vertex_color_import_option=REPLACE（角色）、匯入後設 CTF_USE_COMPLEX_AS_SIMPLE。

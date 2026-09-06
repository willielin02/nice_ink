# Nice Ink — Third-Party Notices

This game includes the following third-party works. Full license texts are
linked or reproduced below. This file ships next to the game executable.

## Fonts

### Noto Sans / Noto Sans JP / Noto Sans TC / Noto Sans SC / Noto Sans KR / Noto Sans Arabic
- Copyright (c) The Noto Project Authors (Google)
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Used as the single UI font family for all 13 languages (menu and in-game HUD; Regular / Bold).
- Sources: https://github.com/notofonts (latin-greek-cyrillic, arabic) and https://github.com/notofonts/noto-cjk (Sans SubsetOTF).

### Oswald
- Copyright (c) 2016 The Oswald Project Authors (https://github.com/googlefonts/OswaldFont)
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Used as the display face (titles, phase banners, hero numbers, primary buttons; Medium / Bold) since 2026-09-06.

### M PLUS Rounded 1c
- Copyright (c) M+ FONTS PROJECT
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Bundled; no longer used by the UI since 2026-09-05 (kept as a fallback face).

### Zen Old Mincho
- Copyright (c) The Zen Project Authors (Yoshimichi Ohira)
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Bundled; no longer used by the UI since 2026-09-05 (kept as a fallback face).

### GenRyuMin (源流明體)
- Copyright (c) ButTaiwan, derived from Source Han Serif (Adobe/Google)
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Used for Traditional Chinese menu text.

### Noto Serif / Noto Serif SC / Noto Serif KR / Noto Naskh Arabic
- Copyright (c) The Noto Project Authors (Google)
- License: SIL Open Font License 1.1 (https://scripts.sil.org/OFL)
- Used for Simplified Chinese, Korean, Cyrillic, extended Latin and Arabic menu text.
- (Noto Serif TC is also bundled as a backup face.)

## Icons

### Lucide
- Copyright (c) 2022 Lucide Contributors; portions (c) 2013-2022 Cole Bemis (Feather)
- License: ISC (https://lucide.dev/license)
- Used for all UI line icons since 2026-09-06 (rendered from the Lucide icon font; SourceAssets/UI/icons/LICENSE_lucide.txt).

### game-icons.net
- Icons adapted from game-icons.net
- License: CC BY 3.0 (https://creativecommons.org/licenses/by/3.0/)
- Authors credited per icon set at https://game-icons.net (Lorc, Delapouite and contributors).
- Used as HUD icons (cup, spray, marker, cash, rotate, eye, trap, sleep, nose, kick).

## 3D Scenes

### "Sauna" by local.yany
- Source: Sketchfab
- License: CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)
- The sauna venue is retained in the project as an alternate stage (L_Sauna).
  If the shipped build excludes L_Sauna, this credit is retained out of courtesy.

### "Radiola from \"Matrix\"" by Sirenko
- Source: Sketchfab (https://sketchfab.com/3d-models/radiola-from-matrix-62beeb98552846bea1f7a2d4b42396a8)
- Author: Sirenko (https://sketchfab.com/sirenko)
- License: CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/)
- Used as the opening-cutscene television (SM_TvRadiola); remains in the dojo as furniture.
- Required credit text (from the model's license.txt): This work is based on
  "Radiola from \"Matrix\"" by Sirenko, licensed under CC-BY-4.0.
- Archive: SourceAssets/Television_Sirenko/ (ATTRIBUTION.txt inside).

### Dojo scene
- Source/license: **PENDING — must be confirmed by the project owner before
  commercial release.** (SourceAssets/dojo carries no attribution record.)

## Textures

### Fundoshi / mawashi cloth (canvas weave)
- Files: `SourceAssets/fundoshi_color.jpg`, `fundoshi_rough.jpg`, `fundoshi_normal.jpg`
  (dated 2025-10-13), and everything derived from them —
  `fundoshi_*_tileable.png`, `fundoshi_color_black.png`, and the engine textures
  `T_FundoshiColor` / `T_FundoshiRough` / `T_FundoshiNormal`.
- Source/license: **PENDING — must be confirmed by the project owner before
  commercial release.** No attribution record exists for these files.
- Note: if these are replaced with a licensed scanned-canvas set, that also
  resolves the "no macro tonal variation" defect measured on 2026-08-18
  (low-frequency std 0.0014 = implausibly uniform for real fabric).

### Input prompt glyphs (mouse buttons / scroll)
- Files: the derived `SourceAssets/InputPrompts/png/T_InMouse*.png` plus the engine textures
  `Content/UI/Input/T_InMouseLeft` / `T_InMouseRight` / `T_InMouseScroll` /
  `T_InMouseMove`.
- Source: **Kenney — Input Prompts 1.5** (www.kenney.nl) —
  `https://kenney.nl/assets/input-prompts`, upstream file `kenney_input-prompts_1.5.zip`.
  The upstream pack itself is **not vendored** (`.gitignore` excludes `*.zip`); it lives
  locally at `SourceAssets/InputPrompts/kenney_input.zip` and is re-downloadable from the
  URL above. Only the four derived PNGs and the licence text are committed.
- License: **CC0 1.0 Universal (public domain)** — commercial use permitted,
  attribution **not required**; credited here anyway. Upstream license text kept
  verbatim at `SourceAssets/InputPrompts/LICENSE_kenney.txt`.
- Modification: recoloured to the project palette (`Paper` / `Red`) and cropped to
  the alpha bounding box so the drawn box equals the ink box (see
  `Docs/UI_SYSTEM.md` §8-5).

## Engine

- Unreal Engine (c) Epic Games, Inc. — distributed under the Unreal Engine EULA.

## Sounds

- (none yet — populate when audio assets are added; keep license + source per file)

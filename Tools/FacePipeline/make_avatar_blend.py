"""Assemble an openable bald-avatar .blend: character16 + the player's face
texture / skin material. Yakuza-sauna version - no hair, no beard: the v4
texture contract already skin-fills scalp hair and the jaw/cheek beard, which
is exactly the bald look the game needs.

Run (out/face_texture.png + out/skin_color.json must already be this player's):
  blender --background --python make_avatar_blend.py -- <out.blend>

All images are packed into the output .blend so it stays valid when the
shared out/ files are overwritten by the next player.
"""
import bpy
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent
CHAR_BLEND = r"C:\games\Unreal Engine\nice_ink\Content\玩家\nice_ink_player_character17.blend"

argv = sys.argv[sys.argv.index("--") + 1:]
OUT_BLEND = Path(argv[0])

# --- open character and rebuild the player's skin material.
# The setup script ends with a save_mainfile (it is designed to run
# standalone on the character blend) - neutralize it here, this scene is
# saved separately as the avatar file.
bpy.ops.wm.open_mainfile(filepath=CHAR_BLEND)
# the file may have been saved in Edit Mode (the user marked vertex groups
# right before saving) - attribute arrays read as empty until Object Mode
if bpy.context.object and bpy.context.object.mode != 'OBJECT':
    bpy.ops.object.mode_set(mode='OBJECT')
_setup_src = (BASE / "setup_skin_material_blender.py").read_text(encoding='utf-8')
_setup_src = _setup_src.replace("bpy.ops.wm.save_mainfile(", "(lambda **kw: None)(")
exec(compile(_setup_src, 'setup_skin_material_blender.py', 'exec'))

# pack per-image: skip orphan datablocks instead of letting pack_all abort
for img in bpy.data.images:
    if img.packed_file or img.source != 'FILE':
        continue
    try:
        img.pack()
    except Exception as e:
        print(f"PACK_SKIP {img.name}: {e}")

OUT_BLEND.parent.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT_BLEND), compress=True)
print("AVATAR_OK", OUT_BLEND)

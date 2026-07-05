import bpy, mathutils, os

bpy.ops.wm.read_factory_settings(use_empty=True)
GLB = r"c:\games\Unreal Engine\nice_ink\SourceAssets\Sauna_localyany\sauna_2k.glb"
OUT_FBX = r"c:\games\Unreal Engine\nice_ink\SourceAssets\Sauna_localyany\sauna_room_repaired.fbx"
TEX_DIR = r"c:\games\Unreal Engine\nice_ink\SourceAssets\Sauna_localyany\textures"
os.makedirs(TEX_DIR, exist_ok=True)
bpy.ops.import_scene.gltf(filepath=GLB)

def world_bbox(objs):
    mins = mathutils.Vector((1e9,)*3); maxs = mathutils.Vector((-1e9,)*3)
    for o in objs:
        if o.type != 'MESH':
            continue
        for c in o.bound_box:
            w = o.matrix_world @ mathutils.Vector(c)
            mins = mathutils.Vector(map(min, mins, w)); maxs = mathutils.Vector(map(max, maxs, w))
    return mins, maxs

meshes = [o for o in bpy.data.objects if o.type == 'MESH']
mins, maxs = world_bbox(meshes)
back = next(o for o in meshes if "Backwall" in o.name)
bmin, bmax = world_bbox([back])
back_y = (bmin.y + bmax.y) * 0.5
cy = (mins.y + maxs.y) * 0.5
front = back.copy(); front.data = back.data.copy(); front.name = "FrontWall_repair"
bpy.context.collection.objects.link(front)
front.matrix_world = mathutils.Matrix.Translation((0.0, 2*(cy - back_y), 0.0)) @ back.matrix_world
print("frontwall placed")

# 材質 -> 貼圖對照 + 貼圖輸出成 PNG
print("=== MATERIAL -> TEXTURES ===")
for mt in bpy.data.materials:
    imgs = []
    if mt.use_nodes:
        for n in mt.node_tree.nodes:
            if n.type == 'TEX_IMAGE' and n.image:
                imgs.append(n.image.name)
    print(f"MAT {mt.name}: {imgs}")

for im in bpy.data.images:
    if im.size[0] == 0:
        continue
    safe = im.name.replace(".", "_").replace(" ", "_")
    fp = os.path.join(TEX_DIR, safe + ".png")
    im.filepath_raw = fp
    im.file_format = 'PNG'
    im.save()
    print(f"TEX SAVED {im.name} -> {safe}.png ({im.size[0]}x{im.size[1]})")

# 匯出純幾何 FBX（無貼圖引用）
bpy.ops.object.select_all(action='SELECT')
bpy.ops.export_scene.fbx(
    filepath=OUT_FBX,
    use_selection=True,
    object_types={'MESH', 'EMPTY'},
    apply_unit_scale=True,
    apply_scale_options='FBX_SCALE_NONE',
    path_mode='STRIP',
    embed_textures=False,
    use_mesh_modifiers=True,
    add_leaf_bones=False,
)
print("EXPORTED:", OUT_FBX)

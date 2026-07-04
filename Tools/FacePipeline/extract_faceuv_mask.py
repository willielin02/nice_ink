"""
Blender headless script: extract FaceUV mask from PlusSize_Male_Body_01.
Run with:
  "C:\\Program Files\\Blender Foundation\\Blender 5.1\\blender.exe" --background --python extract_faceuv_mask.py
"""
import bpy
import numpy as np

BLEND_PATH = r"C:\games\Unreal Engine\nice_ink\Content\玩家\nice_ink_player_character10.blend"
OUTPUT_PATH = r"C:\games\Unreal Engine\nice_ink_face_pipeline\faceuv_mask.png"
TEX_SIZE = 2048
MESH_NAME = "PlusSize_Male_Body_01"


def rasterize_triangle(img, p0, p1, p2):
    pts = np.array([p0, p1, p2], dtype=np.float64)
    min_x = max(0, int(np.floor(pts[:, 0].min())))
    max_x = min(TEX_SIZE - 1, int(np.ceil(pts[:, 0].max())))
    min_y = max(0, int(np.floor(pts[:, 1].min())))
    max_y = min(TEX_SIZE - 1, int(np.ceil(pts[:, 1].max())))

    v0 = pts[2] - pts[0]
    v1 = pts[1] - pts[0]
    dot00 = np.dot(v0, v0)
    dot01 = np.dot(v0, v1)
    dot11 = np.dot(v1, v1)
    denom = dot00 * dot11 - dot01 * dot01
    if abs(denom) < 1e-12:
        return

    inv_denom = 1.0 / denom

    for y in range(min_y, max_y + 1):
        for x in range(min_x, max_x + 1):
            p = np.array([x + 0.5, y + 0.5]) - pts[0]
            dot02 = np.dot(v0, p)
            dot12 = np.dot(v1, p)
            u = (dot11 * dot02 - dot01 * dot12) * inv_denom
            v = (dot00 * dot12 - dot01 * dot02) * inv_denom
            if u >= -0.001 and v >= -0.001 and (u + v) <= 1.001:
                img[y, x] = 255


def main():
    bpy.ops.wm.open_mainfile(filepath=BLEND_PATH)

    obj = bpy.data.objects.get(MESH_NAME)
    if obj is None:
        print(f"ERROR: mesh '{MESH_NAME}' not found")
        return

    # Evaluate depsgraph to get actual UV data (raw mesh has 0 entries in Blender 5.1)
    depsgraph = bpy.context.evaluated_depsgraph_get()
    obj_eval = obj.evaluated_get(depsgraph)
    mesh = obj_eval.to_mesh()

    uv_main = mesh.uv_layers.get("UVMap")
    uv_face = mesh.uv_layers.get("FaceUV")
    if uv_face is None:
        print("ERROR: FaceUV UV layer not found")
        obj_eval.to_mesh_clear()
        return

    print(f"Mesh: {MESH_NAME}, polys: {len(mesh.polygons)}, loops: {len(mesh.loops)}")

    mask = np.zeros((TEX_SIZE, TEX_SIZE), dtype=np.uint8)
    face_poly_count = 0

    for poly in mesh.polygons:
        # Identify face polygons: FaceUV differs from UVMap
        is_face = False
        if uv_main is not None:
            for li in poly.loop_indices:
                mu = uv_main.data[li].uv
                fu = uv_face.data[li].uv
                if abs(mu[0] - fu[0]) + abs(mu[1] - fu[1]) > 0.001:
                    is_face = True
                    break
        else:
            is_face = True

        if not is_face:
            continue

        # Read FaceUV coords and filter by UV range
        uvs_raw = []
        for li in poly.loop_indices:
            u, v = uv_face.data[li].uv
            uvs_raw.append((u, v))

        # Skip stray polygons outside the expected face UV region
        centroid_u = sum(uv[0] for uv in uvs_raw) / len(uvs_raw)
        centroid_v = sum(uv[1] for uv in uvs_raw) / len(uvs_raw)
        if not (0.30 < centroid_u < 0.75 and 0.25 < centroid_v < 0.80):
            continue

        face_poly_count += 1

        px_uvs = [(u * TEX_SIZE, (1.0 - v) * TEX_SIZE) for u, v in uvs_raw]

        for i in range(1, len(px_uvs) - 1):
            rasterize_triangle(mask, px_uvs[0], px_uvs[i], px_uvs[i + 1])

    obj_eval.to_mesh_clear()

    print(f"Face polygons rasterized: {face_poly_count}")
    filled = np.count_nonzero(mask)
    print(f"Mask coverage: {filled} px ({100 * filled / TEX_SIZE**2:.1f}%)")

    # 10px Gaussian feather on edges
    try:
        import cv2
        feathered = cv2.GaussianBlur(mask.astype(np.float32), (0, 0), sigmaX=5)
        mask = np.clip(feathered, 0, 255).astype(np.uint8)
        print("Applied 10px Gaussian feather")
    except ImportError:
        print("WARNING: cv2 not available in Blender Python, saving raw mask")

    # Save via Blender image API (no cv2 dependency for file I/O)
    img = bpy.data.images.new("faceuv_mask", TEX_SIZE, TEX_SIZE, alpha=False)
    mask_norm = mask.astype(np.float32) / 255.0
    mask_flipped = np.flipud(mask_norm).ravel()
    pixels = np.zeros(TEX_SIZE * TEX_SIZE * 4, dtype=np.float32)
    pixels[0::4] = mask_flipped
    pixels[1::4] = mask_flipped
    pixels[2::4] = mask_flipped
    pixels[3::4] = 1.0
    img.pixels.foreach_set(pixels.tolist())
    img.filepath_raw = OUTPUT_PATH
    img.file_format = "PNG"
    img.save()
    print(f"Saved: {OUTPUT_PATH}")


if __name__ == "__main__":
    main()

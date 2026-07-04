"""Headless QA renders of an avatar blend: front + 3/4 head closeup.
Reference only - the acceptance gate is the user's own viewport.

Run:
  blender --background <avatar.blend> --python render_avatar_preview.py -- <out_prefix>
"""
import bpy
import sys
from mathutils import Vector

argv = sys.argv[sys.argv.index("--") + 1:]
PREFIX = argv[0]

TARGET = Vector((0.0, 0.03, 1.53))   # char16 head center

scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE'
scene.render.resolution_x = scene.render.resolution_y = 1024
world = scene.world or bpy.data.worlds.new('World')
scene.world = world
world.use_nodes = True
bg = world.node_tree.nodes.get('Background')
if bg:
    bg.inputs[0].default_value = (0.9, 0.9, 0.9, 1.0)
    bg.inputs[1].default_value = 1.0

cam_data = bpy.data.cameras.new('QA_Cam')
cam_data.lens = 85
cam = bpy.data.objects.new('QA_Cam', cam_data)
scene.collection.objects.link(cam)
scene.camera = cam

key = bpy.data.lights.new('QA_Key', 'SUN')
key.energy = 3.0
key_obj = bpy.data.objects.new('QA_Key', key)
scene.collection.objects.link(key_obj)


def shoot(pos, name):
    cam.location = Vector(pos)
    look = (TARGET - cam.location).to_track_quat('-Z', 'Y').to_euler()
    cam.rotation_euler = look
    key_obj.rotation_euler = look   # light rides the camera: no self-shadow flatness
    scene.render.filepath = f"{PREFIX}_{name}.png"
    bpy.ops.render.render(write_still=True)
    print("RENDERED", scene.render.filepath)


shoot((0.0, -0.85, 1.55), 'front')
shoot((0.62, -0.62, 1.58), 'threequarter')

"""在編輯器視口預覽六具身體的臉（不需 PIE）。

InkBodyActor 的 MID 綁定發生在 BeginPlay（PIE 才生效），編輯器世界的身體
只會顯示 WorldGrid。本腳本給每具 Body_* 掛 transient 預覽 MID：
  FaceTex   = actor 的 FaceTexture 屬性（睜眼/閉眼取決於屬性指向哪張）
  SkinTone  = actor 的 SkinTone 屬性
  MarkerRT/TattooRT = 清成全透明的暫時 RT（不綁會整身變白：母材質預設貼圖是白色）
  InkEyeMask = actor 的 EyeMaskTexture 屬性（未設則維持材質預設全白）

MID 是 transient 的：存地圖前請先清 override（或直接重跑本腳本無妨，
但不要把 override 存進地圖）。在開著的編輯器裡用 VibeUE execute_python_code
執行，或 File > Execute Python Script。
"""
import unreal

sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
mat = unreal.load_asset('/Game/Characters/M_InkBodyChar')
bodies = [a for a in sub.get_all_level_actors()
          if a.get_actor_label().startswith('Body_')]
if not bodies:
    raise RuntimeError('no Body_* actors in the current level')

rt_m = unreal.RenderingLibrary.create_render_target2d(
    bodies[0], 1024, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8)
rt_t = unreal.RenderingLibrary.create_render_target2d(
    bodies[0], 1024, 1024, unreal.TextureRenderTargetFormat.RTF_RGBA8)
for rt in (rt_m, rt_t):
    unreal.RenderingLibrary.clear_render_target2d(
        bodies[0], rt, unreal.LinearColor(0, 0, 0, 0))

for a in bodies:
    mesh = a.get_component_by_class(unreal.StaticMeshComponent)
    mid = mesh.create_dynamic_material_instance(0, mat)
    ft = a.get_editor_property('FaceTexture')
    if ft:
        mid.set_texture_parameter_value('FaceTex', ft)
    mid.set_vector_parameter_value('SkinTone', a.get_editor_property('SkinTone'))
    mid.set_texture_parameter_value('MarkerRT', rt_m)
    mid.set_texture_parameter_value('TattooRT', rt_t)
    try:
        em = a.get_editor_property('EyeMaskTexture')
        if em:
            mid.set_texture_parameter_value('InkEyeMask', em)
    except Exception:
        pass  # 舊版 C++ 沒有這個屬性
    print(a.get_actor_label(), 'preview MID applied')
print('done')

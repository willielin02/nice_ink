# 把 UV0 錨定貼圖的「解剖島內容」從舊版面搬到新版面（2026-08-30）
#
# 只有 body_ao / body_ao_shadow 在島上有真內容（ink_uv_mask_survey 實測：
# 其餘 8 張在島上是常數＝新位置填同一個常數即可；body_chroma_anat 本來就腳本重烘）。
# 作法＝同一個三角形有「舊 UV」與「新 UV」兩份座標 ⇒ 在新 UV 空間光柵化，
# 用重心座標回到舊 UV 取樣。逐紋素精確、無需 Blender bake。
#
# 用法：blender --background --python Tools/AssetPrep/sumo_anat_uv_retarget.py
import bpy, os, shutil
import numpy as np
SA = r"C:\games\Unreal Engine\nice_ink\SourceAssets"
MASTER = os.path.join(SA, "sumo_character_master.blend")
CORR = os.path.join(SA, "anat_uv_correspondence.npz")
# (檔名, 島上是否有內容)；常數者用島上舊值的中位數填新footprint
# **不要手列這張清單** —— 08-30 第一版就是手列的，漏掉了 eye_mask_ink_sumo
# （「靜態禁畫預乘」；島上舊值 1.0＝可畫，新位置 0.49）⇒ user 畫面上乳暈整片畫不上去。
# 現制＝先跑 ink_uv_anchor_scan.py 全掃（SourceAssets + FacePipeline 玩家輸出共 76 張），
# 凡「島@舊UV != 島@新UV」且真的被 UV0 取樣的都必須進來。
# 已判定不需要：face_texture*（UV1）／hair_*（HairUV）／fundoshi_*_tileable（平鋪）／
# T_UI_MarkerPen（UI）／spot_map（未匯入 UE、材質斑駁層是關的）。
_FP = "C:/games/Unreal Engine/nice_ink/Tools/FacePipeline/out/players"
TARGETS = [("body_ao.png", True), ("body_ao_shadow.png", True),
           ("body_height.png", False), ("body_cloth_normal.png", False),
           ("fundoshi_mask_sharp.png", False), ("fundoshi_mask_final.png", False),
           ("fundoshi_mask_sharp_v2.png", False),
           ("fundoshi_edge_shadow.png", False), ("hair_mask.png", False),
           ("face_mask.png", False)]
if os.path.isdir(_FP):
    # 六個玩家的眼罩（含靜態禁畫預乘）——島上是常數 1.0（可畫）
    TARGETS += [(os.path.join(_FP, k, "eye_mask_ink_sumo.png"), False)
                for k in sorted(os.listdir(_FP))]
# **執行期烘出來的臉**：玩家上傳自拍時由 FaceBakery 現烤，不在 Content 裡、
# re-import 碰不到。而且**每個沙箱各有一份**（play_full_flow_4p 用
# -saveddirsuffix ⇒ Saved_P2/P3/P4），受害者常常是別的視窗的角色。
# 08-30 我只列了 Saved/PlayerFace ⇒ 乳暈照樣被擋（乳暈A 新UV=0.0189）。
# **不再挑根目錄：整個專案樹枚舉所有 eye_mask_ink*.png。**
# （同資料夾的 face_open/face_closed 走 UV1、thumb 是 UI ⇒ 不受影響。）
import glob as _g
_ROOT = "C:/games/Unreal Engine/nice_ink"
_eye = set()
for _pat in ("eye_mask_ink.png", "eye_mask_ink_sumo.png"):
    _eye |= set(_g.glob(os.path.join(_ROOT, "**", _pat), recursive=True))
TARGETS += [(f.replace(chr(92), "/"), False) for f in sorted(_eye)]
def P(*a): print(*a, flush=True)

d = np.load(CORR)
uv_old, uv_new = d["uv_old"], d["uv_new"]
bpy.ops.wm.open_mainfile(filepath=MASTER)
me = bpy.data.objects["SumoRetopo"].data
me.calc_loop_triangles(); nt = len(me.loop_triangles)
tl = np.empty(nt*3, np.int64); me.loop_triangles.foreach_get("loops", tl); tl = tl.reshape(nt,3)
moved = ~np.isclose(uv_old, uv_new).all(axis=1)
isl_t = moved[tl].all(axis=1)
P("搬過家的三角形 = %d / %d" % (isl_t.sum(), nt))
TO, TN = uv_old[tl], uv_new[tl]

for name, has_content in TARGETS:
    fp = name if os.path.isabs(name) else os.path.join(SA, name)
    label = (os.path.basename(os.path.dirname(fp)) + "/" + os.path.basename(fp)
             if os.path.isabs(name) else name)
    if not os.path.exists(fp):
        P("%-34s 不存在，跳過" % label); continue
    # 冪等：已經搬過的（存在 _pre_anatuv 備份）直接跳過，重跑安全
    if os.path.exists(fp.replace(".png", "_pre_anatuv.png")):
        P("%-34s 已搬過，跳過" % label); continue
    img = bpy.data.images.load(fp, check_existing=False)
    w, h = img.size
    px = np.empty(w*h*4, np.float32); img.pixels.foreach_get(px)
    px = px.reshape(h, w, 4).copy()
    src = px.copy()
    nfill = 0
    # 舊 footprint 上的值（常數者取中位數）
    const = None
    if not has_content:
        vals = []
        for t in np.nonzero(isl_t)[0][:400]:
            c = TO[t].mean(axis=0)
            vals.append(src[min(h-1,int(c[1]*h)), min(w-1,int(c[0]*w))])
        const = np.median(np.array(vals), axis=0)
    for t in np.nonzero(isl_t)[0]:
        un = TN[t]*np.array([w,h]); uo = TO[t]*np.array([w,h])
        x0=max(0,int(np.floor(un[:,0].min()))-1); x1=min(w-1,int(np.ceil(un[:,0].max()))+1)
        y0=max(0,int(np.floor(un[:,1].min()))-1); y1=min(h-1,int(np.ceil(un[:,1].max()))+1)
        if x1<x0 or y1<y0: continue
        xs,ys=np.meshgrid(np.arange(x0,x1+1)+0.5, np.arange(y0,y1+1)+0.5)
        d0=un[1]-un[0]; d1=un[2]-un[0]
        den=d0[0]*d1[1]-d1[0]*d0[1]
        if abs(den)<1e-9: continue
        vx=xs-un[0,0]; vy=ys-un[0,1]
        b1=(vx*d1[1]-d1[0]*vy)/den; b2=(d0[0]*vy-vx*d0[1])/den
        ins=(b1>=-0.5/max(w,h))&(b2>=-0.5/max(w,h))&(b1+b2<=1.0+0.5/max(w,h))
        if not ins.any(): continue
        if has_content:
            so = uo[0][None,None,:] + b1[...,None]*(uo[1]-uo[0])[None,None,:] \
                                     + b2[...,None]*(uo[2]-uo[0])[None,None,:]
            sx=np.clip(so[...,0].astype(int),0,w-1); sy=np.clip(so[...,1].astype(int),0,h-1)
            blk=px[y0:y1+1, x0:x1+1]
            blk[ins]=src[sy[ins],sx[ins]]
        else:
            blk=px[y0:y1+1, x0:x1+1]
            blk[ins]=const
        nfill += int(ins.sum())
    img.pixels.foreach_set(px.reshape(-1))
    bak = fp.replace(".png","_pre_anatuv.png")
    if not os.path.exists(bak): shutil.copyfile(fp, bak)
    img.filepath_raw = fp; img.file_format='PNG'; img.save()
    P("%-34s %4dx%-4d 寫入 %7d 紋素  %s" % (label, w, h, nfill,
      "重取樣搬運" if has_content else "常數填充"))
    bpy.data.images.remove(img)
P("完成。")

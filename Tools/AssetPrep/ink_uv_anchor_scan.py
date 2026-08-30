# 全面掃描：哪些貼圖在「島的舊 UV」與「島的新 UV」上值不同？（唯讀）
# 2026-08-30：手列清單漏掉了 eye_mask_ink_sumo（靜態禁畫預乘）⇒ 乳暈整片畫不上。
# 這支不猜：把 SourceAssets 與 FacePipeline 玩家輸出的所有 PNG 全掃一遍。
import bpy, os, glob
import numpy as np
SA=r"C:\games\Unreal Engine\nice_ink\SourceAssets"
FP=r"C:\games\Unreal Engine\nice_ink\Tools\FacePipeline\out\players"
d=np.load(os.path.join(SA,"anat_uv_correspondence.npz"))
uv_old,uv_new=d["uv_old"],d["uv_new"]
bpy.ops.wm.open_mainfile(filepath=os.path.join(SA,"sumo_character_master.blend"))
me=bpy.data.objects["SumoRetopo"].data
me.calc_loop_triangles(); nt=len(me.loop_triangles)
tl=np.empty(nt*3,np.int64); me.loop_triangles.foreach_get("loops",tl); tl=tl.reshape(nt,3)
moved=~np.isclose(uv_old,uv_new).all(axis=1)
isl=moved[tl].all(axis=1)
TO,TN=uv_old[tl],uv_new[tl]
idx=np.nonzero(isl)[0][::5]

files=sorted(glob.glob(os.path.join(SA,"*.png")))
for k in sorted(os.listdir(FP)):
    files += sorted(glob.glob(os.path.join(FP,k,"*.png")))
print("掃描 %d 張" % len(files), flush=True)
print("%-46s %-10s %9s %9s %8s" % ("檔案","尺寸","島@舊UV","島@新UV","差異"), flush=True)
bad=[]
for fp in files:
    if "_pre_anatuv" in fp or "_bak" in fp: continue
    try:
        img=bpy.data.images.load(fp,check_existing=False)
        w,h=img.size
        if w<64 or h<64: bpy.data.images.remove(img); continue
        px=np.empty(w*h*4,np.float32); img.pixels.foreach_get(px); px=px.reshape(h,w,4)
    except Exception:
        continue
    def samp(T):
        c=T[idx].mean(axis=1)
        xs=np.clip((c[:,0]*w).astype(int),0,w-1); ys=np.clip((c[:,1]*h).astype(int),0,h-1)
        return px[ys,xs,:3].mean()
    a,b=samp(TO),samp(TN)
    diff=abs(a-b)
    name=os.path.relpath(fp,r"C:\games\Unreal Engine\nice_ink")
    if diff>0.01:
        bad.append((name,w,h,a,b,diff))
    bpy.data.images.remove(img)
for name,w,h,a,b,diff in sorted(bad,key=lambda r:-r[5]):
    print("%-46s %4dx%-5d %9.4f %9.4f %8.4f  **差異**" % (name[:46],w,h,a,b,diff), flush=True)
print("有差異的貼圖 = %d 張" % len(bad), flush=True)

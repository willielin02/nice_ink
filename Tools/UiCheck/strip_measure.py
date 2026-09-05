# -*- coding: utf-8 -*-
"""右緣操作列的版面實測＋一致性閘門（不開引擎）。
user 2026-09-04：「你可以自己量一下畫面中各個間距確認沒問題再給我嗎？」
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
import strip_mock as M

ROWS = M.ROWS
def layout(mouse_mul=1.4, uniform_pitch=False, aligned_verbs=False):
    glyphs=[]
    for verb,kind,val,acc in ROWS:
        gl=[M.kenney(val,M.CAP_H*mouse_mul)] if kind=='mouse' else \
           ([M.cap(val,acc)] if kind=='key' else [M.cap(c) for c in val])
        gw=sum(g.width for g in gl)+2*(len(gl)-1)
        glyphs.append((verb,gl,gw,max(g.height for g in gl)))
    maxgw=max(g[2] for g in glyphs)
    pitch=max(M.ROW_H, max(g[3] for g in glyphs)+4)
    rows=[]; y=M.Y0
    for verb,gl,gw,h in glyphs:
        x = M.RIGHT-gw
        tright = (M.RIGHT-maxgw-8) if aligned_verbs else (x-8)
        rows.append(dict(verb=verb,gh=h,gleft=x,tright=tright,cy=y+(pitch if uniform_pitch else max(M.ROW_H,h+5))/2))
        y += pitch if uniform_pitch else max(M.ROW_H,h+5)
    return rows

def report(name, rows):
    print(f"\n=== {name} ===")
    print(f"{'列':<18}{'glyph高':>8}{'glyph左':>9}{'動詞右緣':>10}{'列中心y':>10}")
    for r in rows:
        print(f"{r['verb']:<18}{r['gh']:>8.1f}{r['gleft']:>9.1f}{r['tright']:>10.1f}{r['cy']:>10.1f}")
    pitch=[round(rows[i+1]['cy']-rows[i]['cy'],1) for i in range(len(rows)-1)]
    tr=[round(r['tright'],1) for r in rows]
    gh=[round(r['gh'],1) for r in rows]
    ok_pitch=len(set(pitch))==1; ok_tr=len(set(tr))==1; ok_gh=len(set(gh))==1
    print(f"  行距           {pitch}   一致={ok_pitch}")
    print(f"  動詞右緣       極差 {max(tr)-min(tr):.1f}px          一致={ok_tr}")
    print(f"  glyph 高度     {gh}   一致={ok_gh}")
    return ok_pitch and ok_tr and ok_gh

a=report("現況版面（我剛才交出去的那張）", layout())
b=report("修正：等距行 ＋ 動詞單一右緣 ＋ glyph 同高(1.0×)",
         layout(mouse_mul=1.0, uniform_pitch=True, aligned_verbs=True))
print("\n間距常數是否落在 NiSpace 4px 網格上：")
for n,v in [("動詞→glyph",8),("鍵帽之間",2),("列高補正",5),("鍵帽內距",12)]:
    print(f"  {n:<12}{v:>3}px   在網格上={v%4==0}")
sys.exit(0 if b else 1)

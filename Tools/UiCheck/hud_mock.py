# -*- coding: utf-8 -*-
"""作畫 HUD 全畫面 mock（1080p 原生）＋尺度呼應閘門。不開引擎，一輪 20 秒。

設計語言＝Meccha（user 2026-09-04 定案「照抄」）：
  無面板／近白 chrome ＋墨色陰影／狀態色三支／圓角只剩鍵帽／
  上緣＝大數字＋祈使句／右緣＝疊放操作提示（G2）／右下＝常駐規則塊

**尺度系統（本檔是唯一來源，之後抄進 NiceInkUiTokens.h）**
  基準 U = 4px @1080p。所有間距與方塊尺寸 = U 的整數倍。
  字級   12 / 16 / 24 / 36    = 3U / 4U / 6U / 9U
  鍵帽高 20 = 5U            邊距 24 = 6U
  列距   40 = 10U           段距 16 = 4U
"""
import io, os, sys, zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SHOT = os.path.join(ROOT, "Saved/Screenshots/WindowsEditor/directdraw_fp_paint.png")
PACK = os.path.join(ROOT, "SourceAssets/InputPrompts/kenney_input.zip")
COOK = os.path.join(ROOT, "Saved/Cooked/Windows/NiceInk/Content/UI/Fonts")
OUT  = os.path.join(ROOT, "Saved/UiMock")

# --- 尺度 ---
U        = 4
S_SMALL, S_BODY, S_TITLE, S_DISPLAY = 3*U, 4*U, 6*U, 9*U      # 12/16/24/36
# 以下三個不是我挑的，是量 Meccha 1080p 實物來的（meccha_2.jpg 右緣，暗磚背景）：
#   元件高 47~48px → 32(glyph)+4(gap)+12(verb) = 48 = 12U
#   列距   61~70px → 64 = 16U
#   右邊距 21px    → 24 = 6U（就近落在網格上）
CAP_H    = 8*U          # 32   ← 我原本猜 20，實測他們是這個的 1.6 倍
MARGIN   = 6*U          # 24
ROW_PITCH= 16*U         # 64
GAP_S, GAP_M, GAP_L = U, 2*U, 4*U                              # 4/8/16
RADIUS   = U            # 鍵帽圓角＝唯一的圓角

# --- 色（中性 chrome：user 09-03「就用中性的」）---
WHITE = (248,248,248)      # 主 chrome
DIMW  = (198,198,198)      # 次級
SHADOW= (16,14,12)         # 陰影＝對比與背景無關
GREEN = (120,214,120)      # 狀態：開啟／可按
AMBER = (232,163,61)       # 一次性動作 / 資源
RED   = (217,79,61)        # 亮起的那顆鍵
INK   = (24,19,15)         # 鍵帽上的深字

def load_font(name,size):
    p=os.path.join(OUT,name+".ttf")
    if not os.path.exists(p):
        b=open(os.path.join(COOK,name+".ufont"),'rb').read()
        os.makedirs(OUT,exist_ok=True); open(p,'wb').write(b[b.find(b'\x00\x01\x00\x00'):])
    return ImageFont.truetype(p,size)

F_SMALL   = load_font("FF_MPlusRounded_Medium", S_SMALL)
F_BODY    = load_font("FF_MPlusRounded_Medium", S_BODY)
F_TITLE   = load_font("FF_MPlusRounded_XBold",  S_TITLE)
F_DISPLAY = load_font("FF_MPlusRounded_XBold",  S_DISPLAY)
F_KEY     = load_font("FF_MPlusRounded_XBold",  S_SMALL)
Z = zipfile.ZipFile(PACK)

def shadow_text(d, xy, text, font, fill, anchor="la"):
    """Meccha 的作法：無面板，靠陰影讓文字與背景無關。"""
    d.text((xy[0]+1,xy[1]+1), text, font=font, fill=SHADOW+(190,), anchor=anchor)
    d.text(xy, text, font=font, fill=fill+(255,), anchor=anchor)

def kenney(name,h,tint=WHITE,accent=RED):
    im=Image.open(io.BytesIO(Z.read(f"Keyboard & Mouse/Default/{name}.png"))).convert('RGBA')
    px=im.load()
    for y in range(im.height):
        for x in range(im.width):
            r,g,b,a=px[x,y]
            if a==0: continue
            if r>180 and g<120 and b<130: px[x,y]=(*accent,a)
            else: px[x,y]=(int(tint[0]*r/255),int(tint[1]*g/255),int(tint[2]*b/255),a)
    return im.resize((max(1,round(h*im.width/im.height)),round(h)),Image.LANCZOS)

def cap(text,fill=WHITE):
    w=max(CAP_H,int(F_KEY.getlength(text))+2*U)
    im=Image.new('RGBA',(w,CAP_H),(0,0,0,0)); d=ImageDraw.Draw(im)
    d.rounded_rectangle([0,0,w-1,CAP_H-1],radius=RADIUS,fill=fill+(240,))
    d.text((w/2,CAP_H/2),text,font=F_KEY,fill=INK+(255,),anchor="mm")
    return im

# --- 內容（作畫相位・鎖定中）---
STRIP=[("ink","mouse","mouse_left"),("ink cups","mouse","mouse_right"),
       ("wash","mouse","mouse_scroll_vertical"),("needle","key","Q"),
       ("shake his dream","key","G"),("stand up","keys","WASD")]
IMPERATIVE="draw on him"
COUNT="47"
RULES=("DRAWING","he wakes when his dream is traced.","don't get recognised later.")

# 舊 HUD 在 720p 截圖上的四塊（實測；先挖掉再畫新的，否則新舊疊在一起）
OLD_HUD = [((536,4,746,78),(0,124)),      # 上緣相位橫幅 → 取下方乾淨膚色
           ((1186,10,1274,40),(0,124)),   # 右上現金
           ((1160,306,1274,406),(-260,0)),# 右緣操作列
           ((612,644,668,686),(0,-140))]  # 下緣墨杯 chip

def clean_plate():
    """挖掉舊 HUD：由矩形四邊的邊界像素做雙線性內插填回去。
    背景是平滑的膚色漸層 ⇒ 內插幾乎看不出接縫（平移補丁會因為漸層而露出方塊）。"""
    im=Image.open(SHOT).convert('RGB'); px=im.load()
    for (box,_) in OLD_HUD:
        x0,y0,x1,y1=box
        top=[px[x,y0-1] for x in range(x0,x1)]; bot=[px[x,y1] for x in range(x0,x1)]
        lef=[px[x0-1,y] for y in range(y0,y1)]; rig=[px[x1,y] for y in range(y0,y1)]
        w=x1-x0; h=y1-y0
        for j in range(h):
            v=j/(h-1) if h>1 else 0
            for i in range(w):
                u=i/(w-1) if w>1 else 0
                c=[]
                for k in range(3):
                    hor=(1-u)*lef[j][k]+u*rig[j][k]
                    ver=(1-v)*top[i][k]+v*bot[i][k]
                    cor=((1-u)*(1-v)*top[0][k]+u*(1-v)*top[w-1][k]
                         +(1-u)*v*bot[0][k]+u*v*bot[w-1][k])
                    c.append(max(0,min(255,int(hor+ver-cor))))
                px[x0+i,y0+j]=tuple(c)
    return im

def render():
    im=clean_plate().resize((1920,1080),Image.LANCZOS)
    d=ImageDraw.Draw(im,'RGBA'); W,H=im.size
    axes={}

    # ① 上緣中央：大數字＋祈使句（無面板）
    cx=W//2
    shadow_text(d,(cx,MARGIN),COUNT,F_DISPLAY,WHITE,anchor="ma")
    shadow_text(d,(cx,MARGIN+S_DISPLAY+GAP_S),IMPERATIVE,F_BODY,DIMW,anchor="ma")
    # 受害者＝這一相位的賭注，跟著上緣走（不放左下角）
    shadow_text(d,(cx,MARGIN+S_DISPLAY+GAP_S+S_BODY+GAP_M),"Willie   ●●○",F_SMALL,DIMW,anchor="ma")

    # ② 右上：資源（現金）
    shadow_text(d,(W-MARGIN,MARGIN),"10,000",F_BODY,AMBER,anchor="ra")
    axes['right']=W-MARGIN

    # ③ 右緣：操作提示（G2 疊放，共用右緣）
    n=len(STRIP); top=H//2-(n*ROW_PITCH)//2
    for i,(verb,kind,val) in enumerate(STRIP):
        gl=[kenney(val,CAP_H)] if kind=='mouse' else ([cap(val)] if kind=='key' else [cap(c) for c in val])
        gw=sum(g.width for g in gl)+GAP_S*(len(gl)-1)
        gx=W-MARGIN-gw; gy=top+i*ROW_PITCH
        for g in gl: im.paste(g,(gx,gy),g); gx+=g.width+GAP_S
        shadow_text(d,(W-MARGIN,gy+CAP_H+GAP_S),verb,F_SMALL,DIMW,anchor="ra")

    # ④ 右下：常駐規則塊（模式名＋兩行怎麼贏）
    by=H-MARGIN
    shadow_text(d,(W-MARGIN,by),RULES[2],F_SMALL,DIMW,anchor="rs")
    shadow_text(d,(W-MARGIN,by-S_SMALL-GAP_S),RULES[1],F_SMALL,DIMW,anchor="rs")
    shadow_text(d,(W-MARGIN,by-2*(S_SMALL+GAP_S)),RULES[0],F_TITLE,GREEN,anchor="rs")

    # ⑤ 下緣中央：目前的墨杯（無面板）
    sw=3*U
    d.rounded_rectangle([cx-sw-GAP_S,H-MARGIN-sw,cx-GAP_S,H-MARGIN],radius=RADIUS,fill=INK+(255,))
    d.rounded_rectangle([cx-sw-GAP_S,H-MARGIN-sw,cx-GAP_S,H-MARGIN],radius=RADIUS,outline=WHITE+(160,),width=1)
    shadow_text(d,(cx+GAP_S,H-MARGIN),"100%",F_BODY,WHITE,anchor="ls")

    axes['left']=MARGIN
    return im,axes

if __name__=="__main__":
    fails=[]
    # 閘門一：所有尺度都是 U 的整數倍
    for n,v in [("S_SMALL",S_SMALL),("S_BODY",S_BODY),("S_TITLE",S_TITLE),("S_DISPLAY",S_DISPLAY),
                ("CAP_H",CAP_H),("MARGIN",MARGIN),("ROW_PITCH",ROW_PITCH),
                ("GAP_S",GAP_S),("GAP_M",GAP_M),("GAP_L",GAP_L),("RADIUS",RADIUS)]:
        if v%U: fails.append(f"{n}={v} 不是 {U} 的整數倍")
    # 閘門二：四角共用同一個邊距
    im,axes=render()
    if axes['right']!=1920-MARGIN or axes['left']!=MARGIN:
        fails.append("邊距不一致")
    # 閘門三：字級是等比階梯
    ratios=[round(S_BODY/S_SMALL,2),round(S_TITLE/S_BODY,2),round(S_DISPLAY/S_TITLE,2)]
    print(f"字級 {S_SMALL}/{S_BODY}/{S_TITLE}/{S_DISPLAY}  倍率 {ratios}")
    if any(r<1.25 or r>1.6 for r in ratios): fails.append(f"字級階梯不勻 {ratios}")
    print("尺度閘門:", "PASS" if not fails else "FAIL "+"; ".join(fails))
    os.makedirs(OUT,exist_ok=True)
    im.save(os.path.join(OUT,"hud_meccha.png"))
    print("wrote", os.path.join(OUT,"hud_meccha.png"))
    print(f"基準 U={U}  邊距 {MARGIN}={MARGIN//U}U  鍵帽 {CAP_H}={CAP_H//U}U  列距 {ROW_PITCH}={ROW_PITCH//U}U")
    sys.exit(1 if fails else 0)

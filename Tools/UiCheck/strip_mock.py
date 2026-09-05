# -*- coding: utf-8 -*-
"""右緣操作列的 glyph 風格離線對照（不開引擎，一輪 15 秒）。

**這支存在的理由**：2026-09-04 第一版 mock 我自己排壞了——鍵帽字沒置中（y 寫死）、
用 9px 畫完再放大 3 倍、還用了 Segoe UI 而不是我們的字。拿自己排壞的圖叫 user 挑，
等於把我的 bug 混進他要判斷的差異裡。

現制三條：
1. **字體＝遊戲同一支**（從 cooked `.ufont` 抽 TTF：4 byte 長度前綴後接 `00010000`）。
   動詞＝M+ Rounded Medium、鍵名＝XBold（對應 C++ 的 bBold）。
2. **幾何＝1080p 原生**（UiScale=1.0），要放大是放大 1080p 的成品，不是放大 9px。
3. **置中有閘門**：量渲染後的墨跡 bbox 在鍵帽內的偏心，>1.0px 就 assert。
   （現況 C++ `DrawKeycap` 本來就是置中的——`EHAlign::Center` ＋ `(H-Size.Y)*0.5`；
   歪的是我的 mock，不是遊戲。）
"""
import io, os, sys, zipfile
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.join(os.path.dirname(__file__), "..", "..")
SHOT = os.path.join(ROOT, "Saved/Screenshots/WindowsEditor/directdraw_fp_paint.png")
PACK = os.path.join(ROOT, "SourceAssets/InputPrompts/kenney_input.zip")
COOK = os.path.join(ROOT, "Saved/Cooked/Windows/NiceInk/Content/UI/Fonts")
OUT  = os.path.join(ROOT, "Saved/UiMock")

PAPER=(242,232,214); INK=(24,19,15); AMBER=(232,163,61); DIM=(168,156,138)
RED=(217,79,61)   # 亮起的那顆滑鼠鍵＝恆用我們的紅（Kenney 原色 231,50,70 不在調色盤裡）
CAP_H=19.0; ROW_H=26.0; RIGHT=1920-26.0; Y0=477.0     # 1080p 原生（UiScale=1）
SMALL=12                                              # NiType::HudSmall

def load_font(name, size):
    p=os.path.join(OUT, name+".ttf")
    if not os.path.exists(p):
        b=open(os.path.join(COOK,name+".ufont"),'rb').read()
        os.makedirs(OUT,exist_ok=True); open(p,'wb').write(b[b.find(b'\x00\x01\x00\x00'):])
    return ImageFont.truetype(p, size)

F_TEXT = load_font("FF_MPlusRounded_Medium", SMALL)
F_KEY  = load_font("FF_MPlusRounded_XBold",  SMALL)
Z = zipfile.ZipFile(PACK)

def kenney(name, h, tint=PAPER, accent=None):
    im=Image.open(io.BytesIO(Z.read(f"Keyboard & Mouse/Default/{name}.png"))).convert('RGBA')
    px=im.load()
    for y in range(im.height):
        for x in range(im.width):
            r,g,b,a=px[x,y]
            if a==0: continue
            if accent and r>180 and g<120 and b<130: px[x,y]=(*accent,a)
            else: px[x,y]=(int(tint[0]*r/255),int(tint[1]*g/255),int(tint[2]*b/255),a)
    return im.resize((max(1,round(h*im.width/im.height)), round(h)), Image.LANCZOS)

def cap(text, accent=False):
    """DrawKeycap 的忠實重現：圓角淺底＋墨字，字**真的**置中。"""
    w=max(int(CAP_H), int(F_KEY.getlength(text)+12))
    im=Image.new('RGBA',(w,int(CAP_H)),(0,0,0,0)); d=ImageDraw.Draw(im)
    d.rounded_rectangle([0,0,w-1,int(CAP_H)-1], radius=4.5, fill=(AMBER if accent else PAPER)+(235,))
    d.text((w/2, CAP_H/2), text, font=F_KEY, fill=INK+(255,), anchor="mm")
    return im

def centering_error(im):
    """墨跡 bbox 相對鍵帽中心的偏移（px）。"""
    a=im.split()[3]
    ink=Image.new('L', im.size, 0); px=im.load(); ipx=ink.load()
    for y in range(im.height):
        for x in range(im.width):
            r,g,b,al=px[x,y]
            if al>128 and r<128: ipx[x,y]=255          # 墨字
    bb=ink.getbbox()
    if not bb: return (0.0,0.0)
    cx=(bb[0]+bb[2])/2; cy=(bb[1]+bb[3])/2
    return (cx-im.width/2, cy-im.height/2)

ROWS=[("ink","mouse","mouse_left",True),("ink cups","mouse","mouse_right",False),
      ("needle","key","Q",False),("shake his dream","key","G",False),
      ("stand up","keys","WASD",False)]
# 13 語最長那組（德文）＝版面壓力測試
ROWS_DE=[("tätowieren","mouse","mouse_left",True),("Farbnäpfe","mouse","mouse_right",False),
         ("Nadel","key","Q",False),("seinen Traum schütteln","key","G",False),
         ("aufstehen","keys","WASD",False)]

GAP_KEY=4.0; GAP_TEXT=8.0; PAD_ROW=4.0; GAP_STACK=2.0

def build_rows(rows, mouse_mul):
    items=[]
    for verb,kind,val,acc in rows:
        if kind=='mouse':  gl=[kenney(val,CAP_H*mouse_mul,accent=RED)]
        elif kind=='key':  gl=[cap(val,acc)]
        else:              gl=[cap(c) for c in val]
        gw=sum(g.width for g in gl)+GAP_KEY*(len(gl)-1)
        items.append([verb,gl,gw,max(g.height for g in gl)])
    return items

def background(h_extra=0):
    im=Image.open(SHOT).convert('RGB').resize((1920,1080), Image.LANCZOS)
    b=(int(RIGHT-420),int(Y0-16),1920,int(Y0+5*ROW_H+40+h_extra))
    im.paste(im.crop((b[0]-460,b[1],b[2]-460,b[3])),(b[0],b[1]))
    return im

def strip(layout, rows=None, mouse_mul=1.3, textcol=None):
    """layout: 'twocol'(我交的) | 'inline'(相鄰) | 'stack'(Meccha)"""
    rows = rows or ROWS
    items = build_rows(rows, mouse_mul)
    maxgw=max(i[2] for i in items); tcol=textcol or PAPER
    tallest=max(i[3] for i in items)
    if layout=='stack':
        pitch = tallest + GAP_STACK + SMALL + PAD_ROW*2
    else:
        pitch = max(ROW_H, tallest+PAD_ROW)
    im=background(int(5*pitch)); d=ImageDraw.Draw(im,'RGBA'); rivers=[]
    for n,(verb,gl,gw,h) in enumerate(items):
        top = Y0 + pitch*n
        gx = RIGHT-gw
        gy = top + (PAD_ROW if layout=='stack' else (pitch-h)/2)
        for g in gl:
            im.paste(g,(int(gx),int(gy+(h-g.height)/2)),g); gx+=g.width+GAP_KEY
        tw=F_TEXT.getlength(verb)
        if layout=='stack':
            d.text((RIGHT, gy+h+GAP_STACK), verb, font=F_TEXT, fill=tcol+(255,), anchor="ra")
        elif layout=='inline':
            tr = RIGHT-gw-GAP_TEXT; rivers.append(GAP_TEXT)
            d.text((tr, top+pitch/2), verb, font=F_TEXT, fill=tcol+(255,), anchor="rm")
        else:
            tr = RIGHT-maxgw-GAP_TEXT; rivers.append(RIGHT-gw-tr)
            d.text((tr, top+pitch/2), verb, font=F_TEXT, fill=tcol+(255,), anchor="rm")
    return im, pitch, rivers, items, maxgw

if __name__ == "__main__":
    fails=[]
    for t in ["Q","G","W","A","S","D","LMB","RMB"]:
        ex,ey=centering_error(cap(t))
        if abs(ex)>1.0 or abs(ey)>1.0: fails.append((t,round(ex,2),round(ey,2)))
    print("鍵帽置中閘門:", "PASS" if not fails else f"FAIL {fails}")
    os.makedirs(OUT,exist_ok=True)
    V=[('twocol',ROWS,"F2  兩欄各自對齊（我上一版交的）"),
       ('inline',ROWS,"G1  相鄰：動詞緊貼 glyph（間距恆定 8）"),
       ('stack', ROWS,"G2  疊放：glyph 上／動詞下，共用右緣（Meccha 實物）"),
       ('stack', ROWS_DE,"G2-de  同上，13 語最長的德文壓力測試")]
    panels=[]; fnt=ImageFont.truetype("C:/Windows/Fonts/YuGothB.ttc",22)
    print("");print(f"{'版面':<8}{'對齊軸':>7}{'空白河極差':>12}{'最壞總寬':>10}{'總高':>8}")
    for lay,rows,title in V:
        im,pitch,rivers,items,maxgw=strip(lay,rows)
        widest=max(F_TEXT.getlength(i[0])+ (0 if lay=='stack' else GAP_TEXT+ (maxgw if lay=='twocol' else i[2])) for i in items)
        axes = 1 if lay=='stack' else 2
        river = f"{max(rivers)-min(rivers):.0f}px" if rivers else "—"
        print(f"{title[:6]:<8}{axes:>7}{river:>12}{widest:>9.0f}px{5*pitch:>7.0f}px")
        b=(int(RIGHT-420),int(Y0-10),1920,int(Y0+5*pitch+10))
        z=im.crop(b); z=z.resize((int(z.width*1.6),int(z.height*1.6)), Image.LANCZOS)
        lab=Image.new('RGB',(z.width,z.height+36),(18,18,18)); lab.paste(z,(0,36))
        ImageDraw.Draw(lab).text((10,7),title,font=fnt,fill=(235,235,235)); panels.append(lab)
    W=max(p.width for p in panels)
    sheet=Image.new('RGB',(W,sum(p.height for p in panels)+24),(18,18,18)); y=0
    for p in panels: sheet.paste(p,(0,y)); y+=p.height+8
    sheet.save(os.path.join(OUT,"layout_axis.png"))
    print("");print("wrote", os.path.join(OUT,"layout_axis.png"), sheet.size)

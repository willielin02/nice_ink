# 家紋 SVG → 一筆畫候選分析器（NiceInk 醉夢描圖圖源調查）
# 每檔：抽閉合子路徑 → 每條當候選路線 → 縮放到 108cm → 三道閘：
#   ①自交避讓（弧距>3cm 點對距 ≥2.6×帶半寬）②pursuit 可描性模擬（前瞻0.9、
#   vmax1.8、60fps，最大偏差<帶半寬）③複雜度（曲率特徵數）
# 產出：kamon_report.txt（逐檔判定）＋ kamon_sheet_*.png（過閘者 contact sheet）
import os, sys, math, json
import numpy as np
from svgpathtools import svg2paths2
from PIL import Image, ImageDraw, ImageFont
from shapely.geometry import Polygon
from shapely.ops import unary_union

sys.stdout.reconfigure(encoding="utf-8")
BASE = os.path.dirname(os.path.abspath(__file__))
KAMON = os.path.join(BASE, sys.argv[1] if len(sys.argv) > 1 else "kamon")
PREFIX = sys.argv[1] if len(sys.argv) > 1 else "kamon"
TARGET_LEN = 108.0
STEP = 0.25
BANDS = {"cup0": 0.30, "cup1": 0.30, "cup2": 0.30}  # user 定案：帶半寬=筆寬（全寬=2×筆寬）
# 窄刺充氣（cm@108 域）：尖刺形剪影兩側走廊會重疊＝真機制障礙非誤殺；
# 外擴讓刺變胖、讀感保留（折鶴喙/尾 msd 0.24→1.03 實錘）
FATTEN = {"crane_origami.svg": 0.4}
# per-file 形態閉合半徑（佔 diag 比例；預設 0.03）：閉合是「橋接不相觸零件」與
# 「焊死細節」的同一把刀——龍的角/爪/鬃在 0.03 被焊成海馬、0.015 全保（08-03 掃描實錘）
CLOSE_FRAC = {"dragon_1f409.svg": 0.015}

def sample_subpaths(svgfile):
    """回傳 [(points Nx2 closed subpath), ...]（各自獨立連續段）"""
    try:
        paths, attrs, svg_attr = svg2paths2(svgfile)
    except Exception:
        return []
    subs = []
    for p in paths:
        for cont in p.continuous_subpaths():
            try:
                L = cont.length()
            except Exception:
                continue
            if not L or L <= 0 or not np.isfinite(L):
                continue
            n = max(int(min(L, 20000) / max(L / 720.0, 1e-6)), 64)
            n = min(n, 900)
            ts = np.linspace(0, 1, n, endpoint=False)
            try:
                pts = np.array([[cont.point(t).real, cont.point(t).imag] for t in ts])
            except Exception:
                continue
            if not np.all(np.isfinite(pts)):
                continue
            start_end = np.linalg.norm(pts[0] - pts[-1])
            bbox = pts.max(0) - pts.min(0)
            diag = float(np.linalg.norm(bbox))
            if diag < 1e-6:
                continue
            closed = start_end < diag * 0.05
            subs.append((pts, closed))
    return subs

def resample_uniform(pts, closed, target_len, step):
    """縮放到 target_len 並均勻弧長重採樣"""
    p = pts.copy()
    if closed:
        p = np.vstack([p, p[:1]])
    seg = np.linalg.norm(np.diff(p, axis=0), axis=1)
    raw = seg.sum()
    if raw < 1e-9:
        return None
    p = p * (target_len / raw)
    p -= p.mean(0)
    seg = np.linalg.norm(np.diff(p, axis=0), axis=1)
    cum = np.concatenate([[0], np.cumsum(seg)])
    n = int(target_len / step)
    s_targets = np.linspace(0, cum[-1], n, endpoint=not closed)
    xs = np.interp(s_targets, cum, p[:, 0])
    ys = np.interp(s_targets, cum, p[:, 1])
    out = np.stack([xs, ys], 1)
    # 輕度平滑（去光柵/貝茲取樣噪聲；~0.75cm 視窗）
    k = 3
    if len(out) > 2 * k + 1:
        kernel = np.ones(2 * k + 1) / (2 * k + 1)
        if closed:
            pad = np.vstack([out[-k:], out, out[:k]])
            sm = np.stack([np.convolve(pad[:, 0], kernel, "valid"),
                           np.convolve(pad[:, 1], kernel, "valid")], 1)
            out = sm
        else:
            sm = out.copy()
            sm[k:-k] = np.stack([np.convolve(out[:, 0], kernel, "valid"),
                                 np.convolve(out[:, 1], kernel, "valid")], 1)
            out = sm
    return out

def min_self_distance(pts, closed, total_len, arc_sep=3.0):
    n = len(pts)
    step = total_len / n
    diff = pts[:, None, :] - pts[None, :, :]
    dist = np.sqrt(np.einsum('ijk,ijk->ij', diff, diff))
    ii, jj = np.meshgrid(np.arange(n), np.arange(n), indexing='ij')
    arc = np.abs(jj - ii) * step
    if closed:
        arc = np.minimum(arc, total_len - arc)
    mask = arc > arc_sep
    if not mask.any():
        return 1e9
    return float(dist[mask].min())

def point_at_arc(pts, closed, total_len, s):
    n = len(pts)
    if closed:
        s = s % total_len
    else:
        s = min(max(s, 0.0), total_len - 1e-6)
    f = s / total_len * n
    i = int(f) % n
    j = (i + 1) % n
    t = f - int(f)
    return pts[i] * (1 - t) + pts[j] * t

def pursuit_sim(pts, closed, total_len, ahead=0.55, vmax=1.8, dt=1 / 30):
    n = len(pts)
    step = total_len / n
    window = max(int(3.0 / step), 2)
    needle = pts[0].copy()
    cur_i, cur_s, prog = 0, 0.0, 0.0
    maxdev = 0.0
    offs = np.arange(-window, window + 1)
    for _ in range(int(total_len / (vmax * dt) * 4) + 100):
        cursor = point_at_arc(pts, closed, total_len, cur_s + ahead)
        to = cursor - needle
        d = math.hypot(to[0], to[1])
        if d > 1e-9:
            needle = needle + to / d * min(vmax * dt, d)
        if closed:
            idx = (cur_i + offs) % n
        else:
            idx = np.clip(cur_i + offs, 0, n - 2)
        jdx = (idx + 1) % n
        A = pts[idx]
        AB = pts[jdx] - A
        L2 = np.einsum('ij,ij->i', AB, AB)
        L2s = np.where(L2 > 1e-12, L2, 1.0)
        tpar = np.clip(np.einsum('ij,ij->i', needle - A, AB) / L2s, 0, 1)
        P = A + AB * tpar[:, None]
        D = needle - P
        dist = np.einsum('ij,ij->i', D, D)
        k = int(np.argmin(dist))
        best_d = math.sqrt(float(dist[k]))
        best_i = int(idx[k])
        best_s = best_i * step + step * float(tpar[k])
        ds = best_s - cur_s
        if closed:
            if ds > total_len / 2: ds -= total_len
            elif ds < -total_len / 2: ds += total_len
        prog += ds
        cur_i, cur_s = best_i, best_s
        maxdev = max(maxdev, best_d)
        done = abs(prog) >= total_len - 0.5 if closed else cur_s >= total_len - 0.5
        if done:
            return maxdev, True
    return maxdev, False

def feature_count(pts, closed):
    """曲率特徵數：轉角密度峰（>25°/cm 視為一個特徵）粗估"""
    n = len(pts)
    ang = []
    rng = range(n) if closed else range(1, n - 1)
    for i in rng:
        a, b, c = pts[(i - 1) % n], pts[i], pts[(i + 1) % n]
        v1, v2 = b - a, c - b
        n1, n2 = np.linalg.norm(v1), np.linalg.norm(v2)
        if n1 < 1e-9 or n2 < 1e-9:
            ang.append(0); continue
        cosv = float(np.clip((v1 @ v2) / (n1 * n2), -1, 1))
        ang.append(math.degrees(math.acos(cosv)))
    ang = np.array(ang)
    # 每點 0.25cm；特徵=連續高轉角區塊
    hi = ang > (25.0 * STEP)
    feats = 0
    prev = False
    for h in hi:
        if h and not prev:
            feats += 1
        prev = h
    return feats

def circleness(pts):
    c = pts.mean(0)
    r = np.linalg.norm(pts - c, axis=1)
    return float(r.std() / max(r.mean(), 1e-9))

def nearest_pair(A, B):
    d = ((A[:, None, :] - B[None, :, :]) ** 2).sum(-1)
    i, j = np.unravel_index(np.argmin(d), d.shape)
    return int(i), int(j)

def splice_one(route, closed, C, gap_frac=0.06):
    """把閉合內輪廓 C 接進 route：外框開缺口→橋入→繞 C 一圈弱閉（留縫）→橋出。
    回傳新 route（拓樸不變：閉合仍閉合）。"""
    i_out, j_in = nearest_pair(route, C)
    g = max(int(len(C) * gap_frac), 4)
    j_in2 = (j_in - g) % len(C)
    # C 的行進：j_in 往前繞到 j_in2（覆蓋 ~94%）
    if j_in <= j_in2:
        arc = C[j_in:j_in2 + 1]
    else:
        arc = np.vstack([C[j_in:], C[:j_in2 + 1]])
    # 出口接回外框：離 C[j_in2] 最近的外框點
    d2 = ((route - C[j_in2]) ** 2).sum(-1)
    i_out2 = int(np.argmin(d2))
    if closed:
        n = len(route)
        # 移除外框上 i_out→i_out2 的短弧（缺口）；保留長弧
        fwd = (i_out2 - i_out) % n
        bwd = (i_out - i_out2) % n
        if fwd <= bwd:
            keep = np.vstack([route[i_out2:], route[:i_out + 1]]) if i_out2 > i_out else route[i_out2:i_out + 1]
            new = np.vstack([keep, arc, keep[:1] * 0 + route[i_out2]])[:-1]
            new = np.vstack([keep, arc])
        else:
            keep = np.vstack([route[i_out:], route[:i_out2 + 1]]) if i_out > i_out2 else route[i_out:i_out2 + 1]
            keep = keep[::-1]  # 以 i_out 為尾
            new = np.vstack([keep, arc])
        # 閉合迴路：尾（arc 末）接回頭（keep 首=i_out2 或反向後首）
        return new, True
    else:
        a, b = (i_out, i_out2) if i_out <= i_out2 else (i_out2, i_out)
        if a == b:
            b = min(a + 1, len(route) - 1)
        if i_out <= i_out2:
            new = np.vstack([route[:a + 1], arc, route[b:]])
        else:
            new = np.vstack([route[:a + 1], arc[::-1], route[b:]])
        return new, False

def collect_interiors(polys, sil, diag):
    """內線候選：①聯集的洞邊界 ②嚴格在剪影內部的色塊輪廓（離外框有邊距）"""
    from shapely.geometry import LineString
    cands = []
    ext = LineString(sil.exterior.coords)
    # ① 洞
    for ring in sil.interiors:
        pts = np.array(ring.coords[:-1])
        if len(pts) >= 8:
            cands.append(pts)
    # ② 內部色塊
    for pg in polys:
        try:
            b = pg.exterior
            pts = np.array(b.coords[:-1])
            if len(pts) < 8:
                continue
            dmin = min(ext.distance(pg.exterior.interpolate(f, normalized=True))
                       for f in np.linspace(0, 1, 24))
            if dmin >= diag * 0.04 and pg.area >= sil.area * 0.01:
                cands.append(pts)
        except Exception:
            pass
    # 大到小、去重（質心相近視為同一條）
    cands.sort(key=lambda c: -abs(Polygon(c).area) if len(c) >= 3 else 0)
    out = []
    for c in cands:
        cc = c.mean(0)
        if all(np.linalg.norm(cc - o.mean(0)) > diag * 0.03 for o in out):
            out.append(c)
    return out[:2]

results = []
ev_by_file = {}
files = sorted(os.listdir(KAMON)) if os.path.isdir(KAMON) else []
print("files:", len(files), flush=True)
for fname in files:
    fpath = os.path.join(KAMON, fname)
    subs = sample_subpaths(fpath)
    if not subs:
        continue
    # 候選一（首選）＝全部閉合子路徑的布林聯集外輪廓＝圖案剪影本體
    #（單抽子路徑=拿到零件：船抽桅杆/櫻花抽單瓣——twemoji 首輪實錘）
    cands = []
    polys = []
    # 開放但填色的路徑（渲染器隱式閉合）也要進聯集：Polygon 自動閉環；
    # 純線條退化成近零面積、被面積門檻自然排除（折鶴 17 面全開放＝首輪實錘）
    for pts, closed in subs:
        if len(pts) >= 8:
            try:
                pg = Polygon(pts)
                if pg.is_valid and pg.area > 0:
                    polys.append(pg)
                else:
                    pg = pg.buffer(0)
                    if pg.area > 0:
                        polys.append(pg)
            except Exception:
                pass
    if polys:
        # 背景板過濾：近矩形且覆蓋幾乎全域的多邊形=畫布底板不是圖
        # （家紋 <rect>、game-icons 的 M0 0h512v512H0z 路徑——兩種都在這裡死）
        gx0 = min(pg.bounds[0] for pg in polys); gy0 = min(pg.bounds[1] for pg in polys)
        gx1 = max(pg.bounds[2] for pg in polys); gy1 = max(pg.bounds[3] for pg in polys)
        gw, gh = max(gx1 - gx0, 1e-9), max(gy1 - gy0, 1e-9)
        def is_backplate(pg):
            x0, y0, x1, y1 = pg.bounds
            covers = (x1 - x0) > 0.88 * gw and (y1 - y0) > 0.88 * gh
            rectish = pg.area > 0.90 * (x1 - x0) * (y1 - y0)
            return covers and rectish
        kept = [pg for pg in polys if not is_backplate(pg)]
        if kept:
            polys = kept
    if polys:
        try:
            # 形態學閉合：膨脹 3% 橋接不相觸零件（鳥居柱樑/串糰子）再收縮還原
            diag = max(max(pg.bounds[2] - pg.bounds[0], pg.bounds[3] - pg.bounds[1]) for pg in polys)
            eps = diag * CLOSE_FRAC.get(fname, 0.03)
            uni = unary_union([pg.buffer(eps) for pg in polys]).buffer(-eps)
            geoms = list(uni.geoms) if hasattr(uni, "geoms") else [uni]
            biggest = max(geoms, key=lambda g: g.area)
            ext = np.array(biggest.exterior.coords[:-1])
            fat = FATTEN.get(fname, 0.0)
            if fat > 0:
                u0 = resample_uniform(ext, True, TARGET_LEN, STEP)
                if u0 is not None:
                    pg2 = Polygon(u0).buffer(fat, join_style=1)
                    if hasattr(pg2, "geoms"):
                        pg2 = max(pg2.geoms, key=lambda g: g.area)
                    ext = np.array(pg2.exterior.coords[:-1])
            # 內線制 08-03 user 定案退役：「專注把外部輪廓做好」——龜殼內圈被讀成
            # 「破一個洞」＝內線在剪影語言裡是噪聲。splice/collect 函式保留備查，不再產生候選。
            combos = []
            for combo in combos:
                route, rclosed = ext.copy(), True
                okbuild = True
                try:
                    for C in combo:
                        route, rclosed = splice_one(route, rclosed, C)
                except Exception:
                    okbuild = False
                if okbuild:
                    u = resample_uniform(route, rclosed, TARGET_LEN, STEP)
                    if u is not None and len(u) >= 80:
                        cands.append((u, rclosed, circleness(u), f'route{len(combo)}'))
            u = resample_uniform(ext, True, TARGET_LEN, STEP)
            if u is not None and len(u) >= 80:
                cands.append((u, True, circleness(u), 'union'))
        except Exception:
            pass
    # 候選二（備選）＝最大的單一子路徑（單體紋樣時聯集=同一條）
    subs = sorted(subs, key=lambda s: -len(s[0]))[:2]
    for pts, closed in subs:
        u = resample_uniform(pts, closed, TARGET_LEN, STEP)
        if u is None or len(u) < 80:
            continue
        circ = circleness(u) if closed else 1.0
        cands.append((u, closed, circ, 'sub'))
    if not cands:
        continue
    # 每檔評最好的一條（非圓、過閘、特徵數 2~9 為佳）
    # 評所有候選再按優先權選：①含內線路線過閘 ②純剪影過閘 ③退路（子路徑最佳）
    #（generic 比較會讓「特徵少的碎片」蓋掉剪影——首輪 torii 支柱事故的制度修）
    evaled = []
    for u, closed, circ, src in cands:
        if closed and circ < 0.03:
            continue  # 外框正圓＝無聊
        msd = min_self_distance(u, closed, TARGET_LEN)
        dev, done = pursuit_sim(u, closed, TARGET_LEN)
        feats = feature_count(u, closed)
        ok0 = bool(done and msd >= 2.6 * BANDS["cup0"] and dev < BANDS["cup0"])
        ok2 = bool(done and msd >= 2.6 * BANDS["cup2"] and dev < BANDS["cup2"])
        rec = dict(file=fname, closed=bool(closed), msd=round(msd, 2), dev=round(dev, 2),
                   done=bool(done), feats=feats, circ=round(circ, 3), src=src,
                   ok_cup0=ok0, ok_cup2=ok2)
        evaled.append((u, rec))
    ev_by_file[fname] = evaled
    best = None
    for u, rec in evaled:
        if rec["src"].startswith('route') and rec["ok_cup0"]:
            best = (u, rec)
            break
    if best is None:
        for u, rec in evaled:
            if rec["src"] == 'union' and rec["ok_cup0"]:
                best = (u, rec)
                break
    if best is None:
        for u, rec in evaled:
            if best is None or (rec["ok_cup0"], -rec["feats"]) > (best[1]["ok_cup0"], -best[1]["feats"]):
                best = (u, rec)
    if best is None:
        continue
    results.append(best)
    r = best[1]
    print(f"{'PASS0' if r['ok_cup0'] else '----'}{'/P2' if r['ok_cup2'] else '   '} "
          f"msd={r['msd']:5.2f} dev={r['dev']:4.2f} feats={r['feats']:2d} "
          f"{'C' if r['closed'] else 'O'} {r['src']:6s} {r['file'][:64]}", flush=True)

# contact sheet（過 cup0 閘者）
passing = [(u, r) for (u, r) in results if r["ok_cup0"]]
print(f"\npassing cup0: {len(passing)} / {len(results)}", flush=True)
COLS, CELL = 6, 200
for sheet_i in range(0, len(passing), 24):
    batch = passing[sheet_i:sheet_i + 24]
    rows = (len(batch) + COLS - 1) // COLS
    img = Image.new("RGB", (COLS * CELL, rows * (CELL + 18)), (18, 17, 28))
    dr = ImageDraw.Draw(img)
    for k, (u, r) in enumerate(batch):
        cx = (k % COLS) * CELL + CELL // 2
        cy = (k // COLS) * (CELL + 18) + CELL // 2
        span = max(u[:, 0].max() - u[:, 0].min(), u[:, 1].max() - u[:, 1].min(), 1e-6)
        sc = (CELL * 0.8) / span
        pix = (u - u.mean(0)) * sc + np.array([cx, cy])
        seq = list(map(tuple, pix)) + ([tuple(pix[0])] if r["closed"] else [])
        dr.line(seq, fill=(185, 179, 214), width=3)
        label = r["file"].replace("Japanese_crest_", "").replace("Japanese_Crest_", "")[:28]
        dr.text(((k % COLS) * CELL + 4, (k // COLS) * (CELL + 18) + CELL + 2),
                f"{sheet_i + k}: {label}", fill=(200, 200, 200))
    out = os.path.join(BASE, f"{PREFIX}_sheet_{sheet_i // 24}.png")
    img.save(out)
    print("sheet:", out, flush=True)

# --- 匯出（烘焙用）：指定檔案的最終路線 ---
# (name, mirror, band_half)：band=該圖所屬杯的帶半寬（筆寬×2.0/1.8/1.6÷2）
# 08-03 user 終定案八式（正推三環淘選全史=帳本）；順序=烘焙表順序。
# 分杯依 feats（轉角特徵數）由易到難：cup0 扇/雙浪/糰子、cup1 折鶴/蛇/龜、cup2 櫻/鳥居
EXPORT = {
    "fan_1faad.svg": ("Fan", True, 0.30),
    "wave_double_1.svg": ("Wave", True, 0.30),
    "dango_1f361.svg": ("Dango", True, 0.30),
    "crane_origami.svg": ("Crane", True, 0.27),
    "snake_1f40d.svg": ("Snake", True, 0.27),
    "turtle_1f422.svg": ("Turtle", True, 0.27),
    "sakura_1f338.svg": ("Sakura", False, 0.24),
    "torii_26e9.svg": ("Torii", False, 0.24),
}
export = []
for fname_e, (name, mirror, band) in EXPORT.items():
    # 用該圖所屬杯的帶重新裁決候選（優先權：內線路線＞剪影＞子路徑）
    picked = None
    for u, r in results:
        if r["file"] != fname_e:
            continue
        cand_pool = ev_by_file.get(fname_e, [])
        for uu, rr in cand_pool:
            ok = rr["done"] and rr["msd"] >= 2.6 * band and rr["dev"] < band
            if ok and rr["src"].startswith("route"):
                picked = (uu, rr)
                break
        if picked is None:
            for uu, rr in cand_pool:
                ok = rr["done"] and rr["msd"] >= 2.6 * band and rr["dev"] < band
                if ok and rr["src"] == "union":
                    picked = (uu, rr)
                    break
        if picked is None:
            for uu, rr in cand_pool:
                if rr["done"] and rr["msd"] >= 2.6 * band and rr["dev"] < band:
                    picked = (uu, rr)
                    break
        break
    if picked is None:
        print(f"EXPORT MISS: {name} (band {band}) — no candidate passes", flush=True)
        continue
    u, r = picked
    export.append(dict(name=name, closed=r["closed"], mirror=mirror, band=band,
                       src=r["src"], msd=r["msd"], dev=r["dev"], feats=r["feats"],
                       points=[[round(float(x), 3), round(float(y), 3)] for x, y in u]))
if export:
    with open(os.path.join(BASE, f"{PREFIX}_export.json"), "w", encoding="utf-8") as f:
        json.dump(export, f)
    print(f"exported {len(export)} motifs -> {PREFIX}_export.json", flush=True)

with open(os.path.join(BASE, f"{PREFIX}_report.json"), "w", encoding="utf-8") as f:
    json.dump([r for (_, r) in results], f, ensure_ascii=False, indent=1)
print("done", flush=True)

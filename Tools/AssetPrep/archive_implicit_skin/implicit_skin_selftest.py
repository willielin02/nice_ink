# implicit_skin.py 的單元自檢（2026-08-27）：用答案已知的形狀驗 HRBF 與合成算子。
# 「先確認儀器真的在動」——不驗這個就去擬 19 塊身體部位，錯了也看不出來。
# Run: python implicit_skin_selftest.py   （純 numpy，不需要 Blender）
import numpy as np
import implicit_skin as isk

FAIL = []


def check(name, ok, detail=""):
    print("  [%s] %s %s" % ("PASS" if ok else "FAIL", name, detail))
    if not ok:
        FAIL.append(name)


def sphere_samples(n, r=0.30, c=np.zeros(3)):
    # 費波那契球：決定性、均勻
    i = np.arange(n) + 0.5
    phi = np.arccos(1.0 - 2.0 * i / n)
    gold = np.pi * (1.0 + 5.0 ** 0.5)
    theta = gold * i
    d = np.stack([np.cos(theta) * np.sin(phi),
                  np.sin(theta) * np.sin(phi), np.cos(phi)], axis=1)
    return c + d * r, d


print("=== 1. HRBF 擬球：f 應該 ≈ 有號距離（外正），|grad| ≈ 1 ===")
C, N = sphere_samples(120, 0.30)
alpha, beta = isk.hrbf_fit(C, N)
# 測試點：球心外 0.05/0.15/0.30/0.45
for rad in (0.05, 0.15, 0.30, 0.45, 0.60):
    P, D = sphere_samples(40, rad)
    f, g = isk.hrbf_eval(C, alpha, beta, P)
    d_true = rad - 0.30
    gn = np.linalg.norm(g, axis=1)
    cos = np.einsum('ij,ij->i', g / gn[:, None], D)
    print("   r=%.2f  f mean=%+.4f (true %+.4f)  |grad| mean=%.3f  cos(grad,radial)=%.4f"
          % (rad, f.mean(), d_true, gn.mean(), cos.mean()))
    if abs(rad - 0.30) < 1e-9:
        check("sphere surface f≈0", abs(f.mean()) < 5e-3, "f=%.5f" % f.mean())
        check("sphere surface |grad|≈1", abs(gn.mean() - 1.0) < 0.05, "%.4f" % gn.mean())
    if rad > 0.30:
        check("sphere outside f>0 (r=%.2f)" % rad, f.mean() > 0, "%.4f" % f.mean())
    if rad < 0.30:
        check("sphere inside f<0 (r=%.2f)" % rad, f.mean() < 0, "%.4f" % f.mean())
    check("grad points outward (r=%.2f)" % rad, cos.mean() > 0.95, "cos=%.4f" % cos.mean())

print("=== 2. 重參數化：iso=0.5 在表面、內 1、外 0 ===")
R = 0.05
check("reparam(0)=0.5", abs(float(isk.reparam(np.array([0.0]), R)[0]) - 0.5) < 1e-12)
check("reparam(-R)=1", abs(float(isk.reparam(np.array([-R]), R)[0]) - 1.0) < 1e-12)
check("reparam(+R)=0", abs(float(isk.reparam(np.array([R]), R)[0]) - 0.0) < 1e-12)
check("reparam 單調遞減", bool(np.all(np.diff(isk.reparam(np.linspace(-R, R, 50), R)) < 0)))

print("=== 3. 數值梯度對賬（合成場的 grad 必須真的是 grad）===")
P = np.array([[0.10, 0.02, 0.0], [0.25, 0.0, 0.05], [0.31, 0.0, 0.0]])
h = 1e-5


def field(X):
    d, gl = isk.hrbf_eval(C, alpha, beta, X)
    return isk.reparam(d, R), gl * isk.reparam_d(d, R)[:, None]


f0, g0 = field(P)
num = np.zeros_like(P)
for k in range(3):
    dp = np.zeros(3)
    dp[k] = h
    num[:, k] = (field(P + dp)[0] - field(P - dp)[0]) / (2 * h)
err = np.linalg.norm(num - g0, axis=1) / np.maximum(np.linalg.norm(num, axis=1), 1e-9)
check("reparam 場梯度 vs 數值差分", float(err.max()) < 1e-3, "rel err max=%.2e" % err.max())

print("=== 4. 合成算子：分離的兩球＝聯集（不遠距混合）===")
C2, N2 = sphere_samples(120, 0.30, np.array([1.50, 0.0, 0.0]))
a2, b2 = isk.hrbf_fit(C2, N2)


def two_field(X):
    d1, g1 = isk.hrbf_eval(C, alpha, beta, X)
    d2, g2 = isk.hrbf_eval(C2, a2, b2, X)
    F = np.stack([isk.reparam(d1, R), isk.reparam(d2, R)], axis=1)
    G = np.stack([g1 * isk.reparam_d(d1, R)[:, None],
                  g2 * isk.reparam_d(d2, R)[:, None]], axis=1)
    return isk.compose(F, G, 2.2, 24.0, -0.35, 0.55, topk=2)


P, _ = sphere_samples(60, 0.30)      # 球 1 表面
f, _ = two_field(P)
check("遠處兩球：表面仍 =0.5", float(np.abs(f - 0.5).max()) < 1e-6,
      "max dev=%.2e" % np.abs(f - 0.5).max())

print("=== 5. 合成算子：接觸時鼓起（梯度反向 → 場值 >0.5）===")
# 兩球心距 0.58 < 2r=0.60 ⇒ 已經接觸。取兩球中間點
C3, N3 = sphere_samples(120, 0.30, np.array([0.58, 0.0, 0.0]))
a3, b3 = isk.hrbf_fit(C3, N3)


def contact_field(X, nmin):
    d1, g1 = isk.hrbf_eval(C, alpha, beta, X)
    d2, g2 = isk.hrbf_eval(C3, a3, b3, X)
    F = np.stack([isk.reparam(d1, R), isk.reparam(d2, R)], axis=1)
    G = np.stack([g1 * isk.reparam_d(d1, R)[:, None],
                  g2 * isk.reparam_d(d2, R)[:, None]], axis=1)
    return isk.compose(F, G, nmin, 24.0, -0.35, 0.55, topk=2)


# 兩球 r=0.30、心距 0.58 ⇒ 交線圓在 x=0.29、半徑 sqrt(0.30²−0.29²)=0.0768
_rr = (0.30 ** 2 - 0.29 ** 2) ** 0.5
_th = np.linspace(0, 2 * np.pi, 24)
ring = np.stack([np.full(24, 0.29), _rr * np.cos(_th), _rr * np.sin(_th)], axis=1)
f_bulge, _ = contact_field(ring, 2.2)
f_sharp, _ = contact_field(ring, 24.0)
print("   接觸環：n_min=2.2 -> f mean=%.4f ; n=24(近 max) -> f mean=%.4f"
      % (f_bulge.mean(), f_sharp.mean()))
check("接觸處 n 小時鼓得比 n 大時多", f_bulge.mean() > f_sharp.mean() + 1e-3)

print()
print("SELFTEST: %d checks failed %s" % (len(FAIL), FAIL if FAIL else ""))

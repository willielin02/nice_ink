# 姿勢邊界階梯（2026-08-27）：掃描器與截圖器的**唯一來源**——同一組姿勢既被量也被渲，
# 圖上的數字才會真的對應那張圖（否則就是「量的線不是他看的線」那條血價）。
# 純資料，不 import bpy。ops = [(bone, axis_idx, deg), ...]


def _bend(d):
    return [("Spine", 0, d * 0.5), ("Spine1", 0, d * 0.5)]


def _hips(d):
    return [("LeftUpLeg", 0, d), ("RightUpLeg", 0, d)]


def _knees(d):
    return [("LeftLeg", 0, d), ("RightLeg", 0, d)]


def _neck(d):
    return [("Neck", 0, d * 0.5), ("Head", 0, d * 0.5)]


LADDER = []
for _d in (0, 10, 15, 20, 25, 30, 45, 64):
    LADDER.append(("bend", "bend%02d" % _d, "軀幹前彎 %d°" % _d, _bend(_d)))
for _d in (0, 15, 25, 30, 45, 60, 90):
    LADDER.append(("hip", "hip%02d" % _d, "髖屈 %d°" % _d, _hips(_d)))
for _d in (0, 60, 90, 120, 144):
    LADDER.append(("knee", "knee%03d" % _d, "膝屈 %d°" % _d, _knees(_d)))
for _d in (-45, -26, 0, 20, 32, 45):
    LADDER.append(("neck", "neck%+03d" % _d, "頭頸 %+d°" % _d, _neck(_d)))


def _hinge(d):
    """髖鉸鏈：骨盆向前傾 d 度，雙腿反向補回 d 度＝腿保持垂直、腳留在地上。
    這才是真人彎腰撿東西的主要動作（脊椎彎只是次要）。"""
    return [("Hips", 0, d, "W"), ("LeftUpLeg", 0, -d, "W"), ("RightUpLeg", 0, -d, "W")]


# 彎腰撿東西（軸向先驗兩個方向，看圖決定哪個是往前）
LADDER.append(("pickup", "hinge_pos30", "髖鉸鏈 +30°（試軸向）", _hinge(30)))
LADDER.append(("pickup", "hinge_neg30", "髖鉸鏈 −30°（試軸向）", _hinge(-30)))
LADDER.append(("pickup", "pick_a", "撿東西A：鉸鏈30°+脊椎10°+膝15°",
               _hinge(30) + _bend(10) + _knees(15)))
LADDER.append(("pickup", "pick_b", "撿東西B：鉸鏈45°+脊椎15°+膝20°",
               _hinge(45) + _bend(15) + _knees(20)))
LADDER.append(("pickup", "pick_c", "撿東西C：鉸鏈60°+脊椎20°+膝25°",
               _hinge(60) + _bend(20) + _knees(25)))
LADDER.append(("pickup", "pick_spine64", "對照：純脊椎彎 64°（現行儀式做法）",
               _bend(64)))

# 候選／對照組
LADDER.append(("cand", "ceremony_pickup", "現行儀式拾瓶 彎64°+頭-26°",
               _bend(64) + _neck(-26)))
LADDER.append(("cand", "bend10_knee90", "膝出高度：彎10°+膝90°",
               _bend(10) + _knees(90)))
LADDER.append(("cand", "bend10_hip25_knee120", "蹲踞式：彎10°+髖25°+膝120°",
               _bend(10) + _hips(25) + _knees(120)))
LADDER.append(("cand", "bend10_hip45_knee120", "蹲踞深：彎10°+髖45°+膝120°",
               _bend(10) + _hips(45) + _knees(120)))

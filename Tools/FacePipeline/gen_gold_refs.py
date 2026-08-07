"""M4 對賬：為金樣本自拍逐張跑 intake_selfie.py，產物收進 refs/<stem>/。

Usage: <venv python> gen_gold_refs.py <refs_root> [selfie...]
預設跑 test_selfies 全部。emma.jpg 預期拒收（Head mask too small）＝行為對照。
"""
import subprocess
import sys
from pathlib import Path

BASE = Path(__file__).resolve().parent


def main():
    root = Path(sys.argv[1])
    paths = [Path(p) for p in sys.argv[2:]] or sorted((BASE / "test_selfies").glob("*.jpg"))
    root.mkdir(parents=True, exist_ok=True)
    for p in paths:
        out = root / p.stem.replace(" ", "_").replace("(", "").replace(")", "")
        out.mkdir(exist_ok=True)
        print(f"=== {p.name} -> {out}", flush=True)
        r = subprocess.run([sys.executable, str(BASE / "intake_selfie.py"), str(p), str(out)],
                           cwd=str(BASE), capture_output=True, text=True, encoding="utf-8",
                           errors="replace")
        (out / "ref_exit.txt").write_text(str(r.returncode), encoding="utf-8")
        (out / "ref_stdout.txt").write_text((r.stdout or "") + "\n" + (r.stderr or ""), encoding="utf-8")
        print(f"    exit={r.returncode}", flush=True)
    print("GOLD_REFS_DONE", flush=True)


if __name__ == "__main__":
    main()

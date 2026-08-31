# 本地全流程自查：簽發→防回滾→escrow 時序→attest quorum。
# 跑法：venv\Scripts\python test_local.py（NICEINK_DEV 由本腳本自設）
import hashlib
import os
import tempfile

os.environ["NICEINK_DEV"] = "1"
os.environ["NICEINK_DB"] = os.path.join(tempfile.mkdtemp(), "test.db")

from fastapi.testclient import TestClient  # noqa: E402

import app as app_mod  # noqa: E402
import store  # noqa: E402
import signing  # noqa: E402

store.DB_PATH = os.environ["NICEINK_DB"]
store.init()
c = TestClient(app_mod.app)

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PublicKey  # noqa: E402

FAILS = []


def check(name, cond):
    print(("PASS" if cond else "FAIL"), name)
    if not cond:
        FAILS.append(name)


H1 = hashlib.sha256(b"blob-v1").hexdigest()
H2 = hashlib.sha256(b"blob-v2").hexdigest()

# 1. 首簽免結算單（新玩家沒有可消除的東西）
r = c.post("/sign-persona", json={"dev_puid": "P1", "blob_sha256": H1})
check("c1 首簽 200", r.status_code == 200 and r.json()["seq"] == 1)
sig1 = r.json()["sig_hex"]

# 2. 簽章可用公開金鑰驗過（遊戲端驗簽的同一條路）
pub = Ed25519PublicKey.from_public_bytes(bytes.fromhex(signing.public_key_hex()))
try:
    pub.verify(bytes.fromhex(sig1), signing.persona_message("P1", 1, H1))
    ok = True
except Exception:
    ok = False
check("c2 簽章驗過", ok)

# 3. 已有帳本＝無結算單拒簽（自行清空的路被封死）
r = c.post("/sign-persona", json={"dev_puid": "P1", "blob_sha256": H2})
check("c3 無結算單 403", r.status_code == 403)

# 4. attest quorum：4 人房需 3 份一致
digest = hashlib.sha256(b"round-1-result").hexdigest()
for i, who in enumerate(["P1", "P2"]):
    r = c.post("/attest", json={"dev_puid": who, "room": "R", "round": 1,
                                "digest": digest, "roster_size": 4})
    check(f"c4.{i} 未達 quorum", r.json()["settled"] is False)
r = c.post("/attest", json={"dev_puid": "P3", "room": "R", "round": 1,
                            "digest": digest, "roster_size": 4})
check("c5 第 3 份達 quorum", r.json()["settled"] is True)
token = r.json()["settlement_token"]

# 5. 不一致的 digest 不計入
r = c.post("/attest", json={"dev_puid": "P4", "room": "R", "round": 2,
                            "digest": hashlib.sha256(b"liar").hexdigest(), "roster_size": 4})
check("c6 異見不結算", r.json()["settled"] is False)

# 6. 有結算單＝簽發放行，序號 +1
r = c.post("/sign-persona", json={"dev_puid": "P1", "blob_sha256": H2,
                                  "settlement_token": token})
check("c7 憑單簽發 seq=2", r.status_code == 200 and r.json()["seq"] == 2)

# 7. 防回滾：latest-seq 永遠指向最新
r = c.get("/latest-seq/P1")
check("c8 latest-seq=2 且 hash=v2", r.json()["seq"] == 2 and r.json()["blob_sha256"] == H2)

# 8. 假 token 拒簽
r = c.post("/sign-persona", json={"dev_puid": "P1", "blob_sha256": H1,
                                  "settlement_token": "f" * 64})
check("c9 假單 403", r.status_code == 403)

# 9. escrow：登記→指認前拒釋出→指認後釋出；slot 衝突拒
r = c.post("/escrow/register", json={"dev_puid": "P2", "room": "R", "round": 1, "slot": 7})
check("c10 escrow 登記", r.status_code == 200)
r = c.post("/escrow/register", json={"dev_puid": "P3", "room": "R", "round": 1, "slot": 7})
check("c11 slot 衝突 409", r.status_code == 409)
r = c.post("/escrow/reveal", json={"dev_puid": "P1", "room": "R", "round": 1,
                                   "accusation_locked": False})
check("c12 指認前拒釋出", r.status_code == 403)
r = c.post("/escrow/reveal", json={"dev_puid": "P1", "room": "R", "round": 1,
                                   "accusation_locked": True})
check("c13 指認後釋出對照", r.status_code == 200 and r.json()["mapping"].get("7") == "P2")

# 10. dev 模式邊界：無身分 401
r = c.post("/sign-persona", json={"blob_sha256": H1})
check("c14 無身分 401", r.status_code == 401)

print()
if FAILS:
    print("RESULT: FAIL", FAILS)
    raise SystemExit(1)
print("RESULT: DONE 14/14")

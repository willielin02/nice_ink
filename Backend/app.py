# Nice Ink 公證後端（規格＝Docs/ANTICHEAT_PLAN.md §4）。
# 職責只有：簽帳本（sign-persona＋序號防回滾）、保管秘密（escrow）、見證結算（attest）。
# 不跑遊戲、不管房間、無 tick。
import hashlib
import math
import uuid

from fastapi import FastAPI, HTTPException
from pydantic import BaseModel, Field

import signing
import steam_auth
import store

app = FastAPI(title="NiceInk Notary", version="0.1")
store.init()


def _auth(body_dict: dict) -> str:
    try:
        return steam_auth.resolve_puid(body_dict)
    except steam_auth.AuthError as e:
        raise HTTPException(status_code=401, detail=str(e))


# ---------- 公鑰（遊戲端驗簽用；也可直接烘進遊戲 config） ----------

@app.get("/pubkey")
def pubkey():
    return {"algo": "ed25519", "format": "NIPS1", "pubkey_hex": signing.public_key_hex()}


# ---------- 簽發 persona（序號單調遞增＝防回滾） ----------

class SignPersonaReq(BaseModel):
    dev_puid: str | None = None
    steam_ticket: str | None = None
    blob_sha256: str = Field(min_length=64, max_length=64)
    settlement_token: str | None = None  # 首簽（無既有序號）可免；其後必附


@app.post("/sign-persona")
def sign_persona(req: SignPersonaReq):
    puid = _auth(req.model_dump())
    existing = store.get_seq(puid)
    if existing is not None:
        # 已有帳本的玩家：任何新簽發必須出示結算單（Phase 1＝host 見證、Phase 2＝quorum，
        # 同一個 token 介面）。沒有這條，客戶端自送「清空版」照樣拿真簽章＝自行消除復活。
        if not req.settlement_token:
            raise HTTPException(status_code=403, detail="已有帳本，簽發需附 settlement_token")
        if store.settlement_get_by_token(req.settlement_token) is None:
            raise HTTPException(status_code=403, detail="settlement_token 無效")
        # TODO(Phase 2)：digest 格式定案後，驗 token 的 digest 內含此 puid 的預期 blob 雜湊
        # ——把「結算過」升級成「結算出的就是這一份」。
    seq = store.bump_seq(puid, req.blob_sha256.lower())
    sig = signing.sign_persona(puid, seq, req.blob_sha256)
    return {"puid": puid, "seq": seq, "sig_hex": sig}


@app.get("/latest-seq/{puid}")
def latest_seq(puid: str):
    row = store.get_seq(puid)
    if row is None:
        return {"puid": puid, "seq": 0, "blob_sha256": None}
    return {"puid": puid, "seq": row[0], "blob_sha256": row[1]}


# ---------- escrow：slot→author 保管（不經 host） ----------

class EscrowRegisterReq(BaseModel):
    dev_puid: str | None = None
    steam_ticket: str | None = None
    room: str
    round: int
    slot: int


@app.post("/escrow/register")
def escrow_register(req: EscrowRegisterReq):
    puid = _auth(req.model_dump())
    ok = store.escrow_register(req.room, req.round, req.slot, puid)
    if not ok:
        raise HTTPException(status_code=409, detail="slot 已被登記")
    return {"ok": True}


class EscrowRevealReq(BaseModel):
    dev_puid: str | None = None
    steam_ticket: str | None = None
    room: str
    round: int
    accusation_locked: bool = False  # 時序鐵則：指認提交後才准釋出


@app.post("/escrow/reveal")
def escrow_reveal(req: EscrowRevealReq):
    _auth(req.model_dump())
    if not req.accusation_locked:
        raise HTTPException(status_code=403, detail="指認未提交，不釋出")
    # TODO(Phase 2)：accusation_locked 不能只信呼叫端宣稱——改為驗「指認事件已進 attest
    # digest」或要求受害者本人的簽名請求。v1 記疤。
    return {"mapping": store.escrow_reveal(req.room, req.round)}


# ---------- attest：回合結算見證（Phase1 host 單見證／Phase2 quorum 同介面） ----------

class AttestReq(BaseModel):
    dev_puid: str | None = None
    steam_ticket: str | None = None
    room: str
    round: int
    digest: str = Field(min_length=64, max_length=64)  # 回合結果摘要 sha256
    self_works: str = ""  # 自己作品雜湊串（防 host 栽贓的自我見證）
    roster_size: int = Field(ge=1, le=6)  # 本回合在場人數（quorum 分母）


@app.post("/attest")
def attest(req: AttestReq):
    puid = _auth(req.model_dump())
    store.attest_put(req.room, req.round, puid, req.digest.lower(), req.self_works)
    rows = store.attest_list(req.room, req.round)
    matching = [r for r in rows if r[1] == req.digest.lower()]
    need = max(1, math.ceil(req.roster_size * 2 / 3))
    if len(matching) >= need:
        token = hashlib.sha256(
            (req.room + str(req.round) + req.digest.lower() + str(uuid.uuid4())).encode()
        ).hexdigest()
        store.settlement_put(req.room, req.round, req.digest.lower(), token)
        return {"settled": True, "have": len(matching), "need": need, "settlement_token": token}
    return {"settled": False, "have": len(matching), "need": need}


# ---------- Phase 3 佔位 ----------

@app.post("/redeem-reset")
def redeem_reset():
    raise HTTPException(status_code=501, detail="Phase 3：待 Steam Inventory 與 TikTok 外部依賴")

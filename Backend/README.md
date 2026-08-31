# Nice Ink 公證後端（薄權威服務）

規格＝`Docs/ANTICHEAT_PLAN.md`。本目錄是 Phase 1 骨架：**本地可跑、零外部帳號**，
部署到正式 hosting 前一切以 dev 模式運作。

## 職責（也是全部的職責）

1. `/sign-persona`：簽發 persona blob（Ed25519 簽 `puid|seq|blob_sha256`，序號單調遞增＝防回滾）
2. `/escrow`：slot→author 對照保管，指認鎖定後才釋出
3. `/attest`：回合結算見證（Phase 1＝host 單見證；Phase 2＝N-of-M quorum，同一格式）
4. `/latest-seq`：進房驗序號
5. `/redeem-reset`：Phase 3 佔位（501）

不跑遊戲、不管房間、無 tick。狀態只有一顆 SQLite。

## 本地開發

```powershell
cd Backend
python -m venv venv
venv\Scripts\pip install -r requirements.txt
venv\Scripts\python gen_keys.py          # 產生 keys/ed25519_private.pem + public 十六進位（貼進遊戲 config）
$env:NICEINK_DEV = "1"                    # dev 模式：跳過 Steam 票證驗證，body 自報 puid
venv\Scripts\uvicorn app:app --port 8787
```

`NICEINK_DEV=1` 時所有請求用 `dev_puid` 欄位自報身分。正式模式（未設）走
Steam Web API `AuthenticateUserTicket`（需 `NICEINK_STEAM_KEY`＋`NICEINK_APPID`）。

## 金鑰紀律

- `keys/` 已 gitignore。私鑰**永不進 repo、永不進客戶端**。
- 遊戲端只拿公鑰（gen_keys.py 印出的十六進位，放遊戲 config 供驗簽）。
- 正式部署：私鑰住 hosting 的 secret manager，不落一般磁碟。

## 簽章格式（遊戲端驗簽要對齊）

```
message = "NIPS1|" + puid + "|" + str(seq) + "|" + blob_sha256_hex（小寫）
sig     = Ed25519(私鑰, message 的 UTF-8 bytes)
```

驗收＝`python test_local.py`（起本地 server 打全流程：簽發→回滾拒收→escrow 時序→attest quorum）。

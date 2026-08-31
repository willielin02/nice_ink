# SQLite 儲存層。後端的全部狀態都在這一顆檔案裡（正式部署可換託管 DB，介面不變）。
import os
import sqlite3
import threading
import time

DB_PATH = os.environ.get(
    "NICEINK_DB", os.path.join(os.path.dirname(os.path.abspath(__file__)), "niceink.db")
)

_lock = threading.Lock()


def _conn() -> sqlite3.Connection:
    c = sqlite3.connect(DB_PATH)
    c.execute("PRAGMA journal_mode=WAL")
    return c


def init() -> None:
    with _lock, _conn() as c:
        c.executescript(
            """
            CREATE TABLE IF NOT EXISTS persona_seq (
                puid TEXT PRIMARY KEY,
                seq INTEGER NOT NULL,
                blob_sha256 TEXT NOT NULL,
                updated_at REAL NOT NULL
            );
            CREATE TABLE IF NOT EXISTS escrow (
                room TEXT NOT NULL,
                round INTEGER NOT NULL,
                slot INTEGER NOT NULL,
                author_puid TEXT NOT NULL,
                revealed INTEGER NOT NULL DEFAULT 0,
                created_at REAL NOT NULL,
                PRIMARY KEY (room, round, slot)
            );
            CREATE TABLE IF NOT EXISTS attest (
                room TEXT NOT NULL,
                round INTEGER NOT NULL,
                puid TEXT NOT NULL,
                digest TEXT NOT NULL,
                self_works TEXT NOT NULL,
                created_at REAL NOT NULL,
                PRIMARY KEY (room, round, puid)
            );
            CREATE TABLE IF NOT EXISTS settlement (
                room TEXT NOT NULL,
                round INTEGER NOT NULL,
                digest TEXT NOT NULL,
                token TEXT NOT NULL,
                created_at REAL NOT NULL,
                PRIMARY KEY (room, round)
            );
            """
        )


def get_seq(puid: str):
    with _lock, _conn() as c:
        row = c.execute(
            "SELECT seq, blob_sha256 FROM persona_seq WHERE puid=?", (puid,)
        ).fetchone()
    return row  # None 或 (seq, blob_sha256)


def bump_seq(puid: str, blob_sha256: str) -> int:
    """原子遞增並記錄最新 blob 雜湊，回傳新序號。"""
    with _lock, _conn() as c:
        row = c.execute("SELECT seq FROM persona_seq WHERE puid=?", (puid,)).fetchone()
        seq = (row[0] + 1) if row else 1
        c.execute(
            "INSERT INTO persona_seq(puid, seq, blob_sha256, updated_at) VALUES(?,?,?,?) "
            "ON CONFLICT(puid) DO UPDATE SET seq=?, blob_sha256=?, updated_at=?",
            (puid, seq, blob_sha256, time.time(), seq, blob_sha256, time.time()),
        )
    return seq


def escrow_register(room: str, rnd: int, slot: int, author_puid: str) -> bool:
    """回 False＝該 slot 已被別人登記（或已釋出）——先到先得，衝突即拒。"""
    with _lock, _conn() as c:
        row = c.execute(
            "SELECT author_puid FROM escrow WHERE room=? AND round=? AND slot=?",
            (room, rnd, slot),
        ).fetchone()
        if row is not None:
            return row[0] == author_puid  # 同人重送＝冪等
        c.execute(
            "INSERT INTO escrow(room, round, slot, author_puid, created_at) VALUES(?,?,?,?,?)",
            (room, rnd, slot, author_puid, time.time()),
        )
    return True


def escrow_reveal(room: str, rnd: int):
    """釋出並標記；回 {slot: author_puid}。呼叫端負責時序閘（指認已提交）。"""
    with _lock, _conn() as c:
        rows = c.execute(
            "SELECT slot, author_puid FROM escrow WHERE room=? AND round=?", (room, rnd)
        ).fetchall()
        c.execute(
            "UPDATE escrow SET revealed=1 WHERE room=? AND round=?", (room, rnd)
        )
    return {slot: author for slot, author in rows}


def attest_put(room: str, rnd: int, puid: str, digest: str, self_works: str) -> None:
    with _lock, _conn() as c:
        c.execute(
            "INSERT OR REPLACE INTO attest(room, round, puid, digest, self_works, created_at) "
            "VALUES(?,?,?,?,?,?)",
            (room, rnd, puid, digest, self_works, time.time()),
        )


def attest_list(room: str, rnd: int):
    with _lock, _conn() as c:
        return c.execute(
            "SELECT puid, digest, self_works FROM attest WHERE room=? AND round=?",
            (room, rnd),
        ).fetchall()


def settlement_put(room: str, rnd: int, digest: str, token: str) -> None:
    with _lock, _conn() as c:
        c.execute(
            "INSERT OR REPLACE INTO settlement(room, round, digest, token, created_at) "
            "VALUES(?,?,?,?,?)",
            (room, rnd, digest, token, time.time()),
        )


def settlement_get_by_token(token: str):
    with _lock, _conn() as c:
        return c.execute(
            "SELECT room, round, digest FROM settlement WHERE token=?", (token,)
        ).fetchone()

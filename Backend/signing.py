# Ed25519 簽章層。簽章格式（遊戲端驗簽必須逐字對齊，見 README）：
#   message = "NIPS1|" + puid + "|" + str(seq) + "|" + blob_sha256_hex(小寫)
import os

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

PRIV_PATH = os.environ.get(
    "NICEINK_PRIVKEY",
    os.path.join(os.path.dirname(os.path.abspath(__file__)), "keys", "ed25519_private.pem"),
)

_key: Ed25519PrivateKey | None = None


def _load() -> Ed25519PrivateKey:
    global _key
    if _key is None:
        with open(PRIV_PATH, "rb") as f:
            k = serialization.load_pem_private_key(f.read(), password=None)
        if not isinstance(k, Ed25519PrivateKey):
            raise TypeError("keys/ed25519_private.pem 不是 Ed25519 私鑰")
        _key = k
    return _key


def persona_message(puid: str, seq: int, blob_sha256: str) -> bytes:
    return f"NIPS1|{puid}|{seq}|{blob_sha256.lower()}".encode("utf-8")


def sign_persona(puid: str, seq: int, blob_sha256: str) -> str:
    return _load().sign(persona_message(puid, seq, blob_sha256)).hex()


def public_key_hex() -> str:
    return (
        _load()
        .public_key()
        .public_bytes(
            encoding=serialization.Encoding.Raw,
            format=serialization.PublicFormat.Raw,
        )
        .hex()
    )

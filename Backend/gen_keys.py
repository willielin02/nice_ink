# 產生後端簽章金鑰對（Ed25519）。私鑰落 keys/（gitignored），公鑰印出供遊戲 config。
import os

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

KEYS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "keys")
PRIV_PATH = os.path.join(KEYS_DIR, "ed25519_private.pem")


def main() -> None:
    os.makedirs(KEYS_DIR, exist_ok=True)
    if os.path.exists(PRIV_PATH):
        raise SystemExit(f"已存在 {PRIV_PATH}——不覆蓋既有私鑰。要重生請先手動刪除（舊簽章會全部失效）。")
    key = Ed25519PrivateKey.generate()
    pem = key.private_bytes(
        encoding=serialization.Encoding.PEM,
        format=serialization.PrivateFormat.PKCS8,
        encryption_algorithm=serialization.NoEncryption(),
    )
    with open(PRIV_PATH, "wb") as f:
        f.write(pem)
    pub = key.public_key().public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )
    print("私鑰已寫入:", PRIV_PATH)
    print("公鑰（貼進遊戲 config 供驗簽）:", pub.hex())


if __name__ == "__main__":
    main()

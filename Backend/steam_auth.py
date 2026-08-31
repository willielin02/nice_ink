# 身分驗證：正式模式走 Steam Web API AuthenticateUserTicket（玩家零感知，搭票證便車）；
# NICEINK_DEV=1 時跳過、body 自報 dev_puid（本地開發／遊戲端接線期）。
import os

import requests

DEV_MODE = os.environ.get("NICEINK_DEV") == "1"
STEAM_KEY = os.environ.get("NICEINK_STEAM_KEY", "")
APPID = os.environ.get("NICEINK_APPID", "")

VERIFY_URL = "https://partner.steam-api.com/ISteamUserAuth/AuthenticateUserTicket/v1/"


class AuthError(Exception):
    pass


def resolve_puid(body: dict) -> str:
    """回傳已驗證的玩家身分（正式＝SteamID64 字串；dev＝自報 puid）。"""
    if DEV_MODE:
        puid = body.get("dev_puid")
        if not puid:
            raise AuthError("dev 模式需要 dev_puid")
        return str(puid)
    ticket = body.get("steam_ticket")
    if not ticket:
        raise AuthError("缺 steam_ticket")
    if not STEAM_KEY or not APPID:
        raise AuthError("後端未配置 NICEINK_STEAM_KEY / NICEINK_APPID")
    r = requests.get(
        VERIFY_URL,
        params={"key": STEAM_KEY, "appid": APPID, "ticket": ticket},
        timeout=10,
    )
    r.raise_for_status()
    data = r.json().get("response", {}).get("params", {})
    if data.get("result") != "OK":
        raise AuthError(f"Steam 驗票失敗: {data}")
    if data.get("vacbanned") or data.get("publisherbanned"):
        raise AuthError("帳號被封禁")
    return str(data["steamid"])

#!/usr/bin/env python3
"""
個股 K 線圖繪製 — 讀 ProTech-Stock-News (PostgreSQL `twsestock`) 的 daily_kline，
用 mplfinance 畫蠟燭圖（含成交量 + MA5/MA20/MA60），輸出 PNG。

資料來源：ProTech-Stock-News-20260603 的 src.core.pg_client.get_daily_kline()
（daily_kline 表：trade_date/open/high/low/close/volume）。

用法：
    python draw_kline.py 2330                 # 近 120 日
    python draw_kline.py 2330 --days 250       # 近 250 日
    python draw_kline.py 2317 --out /tmp/x.png

需在 ProTech-Stock-News 專案的 venv 下執行（含 psycopg2 + .env DB 設定 + mplfinance）。
"""
import argparse
import os
import sys

import pandas as pd
import mplfinance as mpf
import matplotlib
from matplotlib import font_manager

# Register a CJK font so Chinese stock names render (not tofu boxes).
_CJK_FONT = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
if os.path.exists(_CJK_FONT):
    try:
        font_manager.fontManager.addfont(_CJK_FONT)
        _name = font_manager.FontProperties(fname=_CJK_FONT).get_name()
        matplotlib.rcParams["font.family"] = _name
        matplotlib.rcParams["axes.unicode_minus"] = False  # keep minus sign ascii
    except Exception:
        pass

# Reuse the ProTech project's DB client + .env (PG host/user/db).
PROJ = os.path.expanduser("~/projects/ProTech-Stock-News-20260603")
sys.path.insert(0, PROJ)
os.chdir(PROJ)  # so python-dotenv / relative paths resolve like the app
try:
    from dotenv import load_dotenv
    load_dotenv(os.path.join(PROJ, ".env"))
except Exception:
    pass

from src.core.pg_client import get_daily_kline, get_all_stocks  # noqa: E402


def stock_name(code: str) -> str:
    try:
        for s in get_all_stocks():
            if s["code"] == code:
                return s["name"]
    except Exception:
        pass
    return ""


def main():
    ap = argparse.ArgumentParser(description="Draw a candlestick K-line chart for a stock.")
    ap.add_argument("code", help="stock code, e.g. 2330")
    ap.add_argument("--days", type=int, default=120, help="number of recent trading days (default 120)")
    ap.add_argument("--out", default=None, help="output PNG path (default experiments/stock-kline/out/<code>_kline.png)")
    args = ap.parse_args()

    rows = get_daily_kline(args.code, days=args.days)
    if not rows:
        sys.exit(f"ERROR: no daily_kline data for stock_code={args.code}")

    # Build a DatetimeIndex OHLCV frame as mplfinance expects.
    df = pd.DataFrame(rows)
    df["Date"] = pd.to_datetime(df["date"])
    df = df.set_index("Date").rename(columns={
        "open": "Open", "high": "High", "low": "Low",
        "close": "Close", "volume": "Volume",
    })[["Open", "High", "Low", "Close", "Volume"]]

    name = stock_name(args.code)
    title = f"{args.code} {name}  ({df.index[0].date()} ~ {df.index[-1].date()}, {len(df)} bars)"

    out = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                   "out", f"{args.code}_kline.png")
    os.makedirs(os.path.dirname(out), exist_ok=True)

    # Candle chart: red-up / green-down (TW market convention), volume panel, MA lines.
    mc = mpf.make_marketcolors(up="red", down="green", edge="inherit",
                               wick="inherit", volume="in")
    _rc = {}
    if matplotlib.rcParams.get("font.family"):
        _rc["font.family"] = matplotlib.rcParams["font.family"]
    style = mpf.make_mpf_style(marketcolors=mc, gridstyle=":", y_on_right=True, rc=_rc)

    mpf.plot(
        df, type="candle", style=style, title=title,
        mav=(5, 20, 60), volume=True, figratio=(16, 9), figscale=1.2,
        tight_layout=True, savefig=dict(fname=out, dpi=130, bbox_inches="tight"),
    )
    print(f"OK wrote {out}  ({len(df)} bars, {df.index[0].date()}..{df.index[-1].date()})")


if __name__ == "__main__":
    main()

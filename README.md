# daytrader

A C++20 terminal dashboard for discretionary ETF day trading with the official
Interactive Brokers TWS API. It monitors market context, sector and industry
rotation, VWAP entry timing, Level-1 Order Flow, and open-position guidance.

The application is decision support only and does not submit orders.

## Trading model

The unleveraged ETF determines direction while its leveraged counterpart is the
manual execution instrument. The main flow-confirmed pairs are `QQQ -> TQQQ`
and `SOXX -> SOXL`.

SPY and QQQ provide market context, but they do not veto an independently
strong sector or industry. Each instrument retains its own signal:

- `phase`: `BUILDING`, `STRONG`, `NEUTRAL`, `FADING`, or `WEAK`
- `score`: 0-100 bullish structure score; this is not a probability
- `entry`: `WATCH`, `WAIT_VWAP`, `WAIT_FLOW`, `READY`, or `AVOID`
- `if held`: `HOLD`, `PROTECT`, `TRIM`, or `EXIT`
- `P30/N`: empirical 30-minute outcome probability and resolved sample count

The analysis combines:

- independently anchored extended-hours (`EXT`) and regular-hours (`RTH`) VWAP
- separate VWAP/ATR entry zones for signal and leveraged ETFs
- time-of-day Relative Volume (`RVOL`)
- 15/30/60-minute relative strength against SPY and QQQ
- 30/60-second trade DeltaRatio and Level-1 OFI
- spread, microprice skew, ATR response, and pressure absorption

RTH VWAP is active from 09:30-16:00 New York. Outside that window the entry
zone uses the current extended-hours segment. A leveraged entry reaches `READY`
only when its own entry zone and the signal ETF's Order Flow agree.

For an observed position, the dashboard tracks average cost, mark, unrealized
P&L, peak MFE, and profit giveback. Once peak profit reaches 0.25% of cost basis,
20%/35%/50% giveback changes guidance to `PROTECT`/`TRIM`/`EXIT`.

## Dashboard

The terminal contains five pages:

1. Market: SPY, QQQ, TQQQ execution, and optional VIX context
2. Sectors: eleven standard US sector ETFs
3. Industries: twenty industry ETFs
4. Leveraged: signal-to-long-leveraged ETF mappings and execution state
5. Trade: filtered day-trade positions, P30 calibration, and live Order Flow

Use `Tab`, Left/Right, or `1`-`5` to switch pages. Use `[` and `]` for paginated
rotation groups. Tables adapt to the current terminal size.

## Requirements

- TWS or IB Gateway running with socket clients enabled
- Official Mac/Unix TWS API under `$HOME/IBJts` (validated with Latest `10.49.02`)
- `protobuf@21` (`brew install protobuf@21`)
- `libbid.a` under `$HOME/IBJts/source/cppclient/client/lib/`
- Suitable IBKR US ETF market-data subscriptions

Override dependency paths when configuring CMake:

```sh
cmake -S . -B build \
  -DIBKR_TWS_API_ROOT=/path/to/IBJts \
  -DIBKR_LIBBID_PATH=/path/to/libbid.a
```

IBKR distributes TWS API 10.49 and newer under GPLv3; consider the license
before distributing a linked binary.

## Build and run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/daytrader
```

Defaults:

- TWS: `127.0.0.1:9972`
- historical/monitor client ID: `7`
- live positions, prices, and Order Flow client ID: `8`
- data: complete one-minute bars with five-minute trend context
- time zone: `America/New_York`

Streaming prices, P&L, and Order Flow redraw at roughly one-second cadence;
execution signals update on completed one-minute bars. The Trade page shows the
actual `marketDataType` received from IBKR. VIX is optional, and unavailable VIX
data does not block ETF scans. Inverse ETFs are displayed only as references.

## Backtests and local data

Run the intraday strategy over approximately 240 calendar days:

```sh
./build/daytrader backtest 240
```

Run the Order Flow experiment for 30 days or year to date:

```sh
./build/daytrader orderflow-backtest 30
./build/daytrader orderflow-backtest ytd
```

One-minute bars are cached under `data/ibkr/all_1m/`; raw Order Flow events are
cached under `data/ibkr/order_flow_ticks/`. Both are ignored by Git. Cached
history is reused and successful downloads are persisted incrementally. The
client does not add an artificial pacing delay; actual IBKR rate-limit responses
are surfaced and retried.

The backtest uses the unleveraged ETF for direction and the leveraged ETF for
execution, combines five-minute trend with one-minute timing, and includes all
cached timestamps, including extended hours. There is no daily trade-count cap;
a new entry is armed after the prior continuous `READY` wave resets.

Historical Order Flow is used only when matching ticks are available. Missing
flow is reported rather than treated as confirmation. Backtest results include
estimated transaction costs and do not imply future performance.

## P30 calibration

When a `BUILDING` or `READY` wave begins, its entry price, ATR, RVOL, relative
strength, DeltaRatio, OFI, spread, and session are stored. Within 30 minutes:

- reaching `+0.75 ATR` before `-0.40 ATR` is `SUCCESS`
- reaching `-0.40 ATR` first, or timing out, is `FAILURE`
- touching both levels in one one-minute bar is `AMBIGUOUS` and excluded

`P30` is Beta-smoothed and displayed with `N`, a Wilson 95% interval, and its
calibration scope. A dash means more observations are needed. Records persist
across restarts in `data/ibkr/setup_outcomes.csv` and reconnecting does not
duplicate an uninterrupted signal wave.

## Common configuration

| Variable | Default |
| --- | --- |
| `DAYTRADER_IBKR_HOST` | `127.0.0.1` |
| `DAYTRADER_IBKR_PORT` | `9972` |
| `DAYTRADER_IBKR_CLIENT_ID` | `7` |
| `DAYTRADER_LIVE_CLIENT_ID` | `8` |
| `DAYTRADER_TIME_ZONE` | `America/New_York` |
| `DAYTRADER_MINUTE_DATA_DIR` | `data/ibkr/all_1m` |

Additional backfill and request overrides are defined in
[`src/config/AppConfig.cpp`](src/config/AppConfig.cpp).

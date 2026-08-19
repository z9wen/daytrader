# daytrader

A C++20 terminal dashboard for discretionary ETF day trading with the official
Interactive Brokers TWS API. It continuously evaluates market regime, sector
and industry rotation, VWAP entry zones, and long-position management guidance.

The application is decision support only: it does not submit orders.

## Current workflow

The unleveraged ETF determines direction and the leveraged ETF supplies the
price and VWAP/ATR timing used for manual execution. `QQQ -> TQQQ` and
`SOXX -> SOXL` are the two flow-confirmed execution pairs.

Each rotation row separates trend from timing:

- `phase`: `BUILDING`, `STRONG`, `NEUTRAL`, `FADING`, or `WEAK`
- `score`: transparent 0-100 bullish-structure score, not a calibrated probability
- `entry`: `WATCH`, `WAIT_VWAP`, `WAIT_FLOW`, `READY`, or `AVOID`
- `if held`: `HOLD`, `PROTECT`, `TRIM`, or `EXIT`
- independent VWAP/ATR entry zones for the signal and leveraged ETFs
- time-of-day Relative Volume (`RVOL`) against the median of up to 20 prior sessions
- VWAP structure (`RECLAIM`, `RISING`, `ABOVE`, `BELOW`, or `LOST`)
- 15/30/60-minute relative strength against both SPY (`S`) and QQQ (`Q`)

Sector and industry phases remain instrument-specific. The SPY/QQQ market
regime contributes to the score as risk context, but a bearish market does not
force an independently strong industry to `WEAK` or every held position to
`EXIT`.

The terminal has five pages: market context (`SPY`, `QQQ`, `TQQQ` execution,
and optional `VIX`), the eleven standard sector ETFs, twenty industry ETFs,
an industry-derived long-leveraged ETF watchlist, and a live trade page. The
leveraged page keeps mappings such as `SOXX` to `SOXL`, the leveraged price and
entry zone, while the Sectors and Industries pages show each signal ETF's
current price before RVOL.
The trade page filters the IBKR account to positions in the configured signal
and long-leveraged ETF universe, so long-term bond or income holdings do not
appear as day trades. It shows average cost, mark, unrealized P&L,
process-observed peak MFE, profit giveback, and live holding guidance, plus rolling
30/60-second DeltaRatio for QQQ and SOXX, ATR-normalized pressure state, and
evidence quality. A new `TQQQ` or `SOXL` entry reaches `READY` only when the
leveraged ETF is in its own entry zone and QQQ/SOXX Order Flow is
`BUY_EFFECTIVE` or shows `SELLING_ABSORBED`. Evidence quality remains a visible
continuous diagnostic and is not used as an arbitrary pass/fail cutoff. Once peak unrealized profit
reaches 0.25% of cost basis, 20%/35%/50% giveback tightens guidance to
`PROTECT`/`TRIM`/`EXIT`. Press `Tab`, Left/Right, or `1`/`2`/`3`/`4`/`5` to switch
pages. Interactive
tables automatically select a regular, compact, or minimal column set from the
current terminal width. Rotation rows
are separated into clearly labeled `STRONG`, `NEUTRAL`, and `WEAK` sections and
paginated to the available height; press `[` or `]` to change page. A group that
continues on another page repeats its label and column header. Resizing the
terminal triggers a responsive redraw without requesting new market data.

## Prerequisites

- A running TWS or IB Gateway with socket clients enabled.
- The official Mac/Unix TWS API extracted to `$HOME/IBJts`. The current build
  is validated with Latest `10.49.02`; CMake detects and prints the installed
  SDK version instead of assuming one.
- `protobuf@21`, installed with `brew install protobuf@21`.
- Intel Decimal Floating-Point `libbid.a` at
  `$HOME/IBJts/source/cppclient/client/lib/libbid.a`.
- Appropriate IBKR API market-data subscriptions for the requested US ETFs.

The SDK location can be changed with `-DIBKR_TWS_API_ROOT=/path/to/IBJts`, and
the decimal library with `-DIBKR_LIBBID_PATH=/path/to/libbid.a`. Neither
dependency is committed to this repository.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The first build regenerates and compiles the protocol-buffer sources supplied
by IBKR, so it takes longer than subsequent builds. IBKR distributes API
`10.49` and newer under GPLv3, which matters if the resulting application is
distributed rather than kept as a local tool.

## Continuous monitor

The defaults connect to TWS at `127.0.0.1:9972` with client ID `7`, preserve
complete one-minute `TRADES` bars including premarket and after-hours, derive a
five-minute trend layer, and display times in `America/New_York`. A second
connection uses client ID `8` for positions/P&L, streaming Level-1 prices for
the complete monitored universe, and QQQ/SOXX Last + BidAsk tick-by-tick flow.
The Trade page reports the feed type received through IBKR's
`marketDataType` callback (`LIVE`, `FROZEN`, `DELAYED`, or `DELAYED_FROZEN`),
so the UI does not infer real-time status from configuration.
The monitor merges live updates into the CSV cache and backfills missing
one-minute history before scanning. Time-of-day RVOL uses the
median of up to 20 cached prior sessions no older than 45 calendar days; it
remains unavailable until at least three comparable sessions have accumulated.

```sh
./build/daytrader
```

Execution analysis refreshes after each synchronized completed one-minute bar;
streaming price, live P&L, and Order Flow redraw at roughly one-second cadence.
Inverse ETFs are
shown only as bearish references; they are not subscribed or proposed as live
short trades.

## Historical backtest and local data

Run the current QQQ-to-TQQQ and SOXX-to-SOXL intraday rules over approximately
240 calendar days:

```sh
./build/daytrader backtest 240
```

Complete one-minute bars are stored as one CSV per symbol under
`data/ibkr/all_1m/`:

- `SPY.csv`
- `QQQ.csv`
- `TQQQ.csv`
- `SOXX.csv`
- `SOXL.csv`

The cache is ignored by Git. A complete recent cache avoids TWS requests;
otherwise the program refreshes only recent bars or downloads history only for
missing symbols. Every successful IBKR batch is persisted immediately.
For one-minute bars, a range of up to 365 calendar days is requested in one
window per symbol; only longer ranges are divided at IBKR's documented 365-day
boundary. Requests are submitted without a client-side pacing delay. If TWS
actually returns a pacing/rate-limit response, the failed request is retried
without shortening or silently discarding its requested range.

The unleveraged ETF determines direction while the leveraged ETF must reach its
own VWAP/ATR entry zone. The backtest does not require SPY and QQQ to be bullish,
combines five-minute trend context with one-minute execution, fills at the next
one-minute bar open, and evaluates every timestamp present in the complete IBKR
cache, including premarket and after-hours. There is no built-in daily
trade-count cap; a new entry is armed only after the previous continuous READY
wave genuinely resets. The report separates the first trade from later
re-entries instead of silently limiting their count.

Live DeltaRatio/Order Flow is not replayed in the baseline unless historical
ticks are available, so the report labels that limitation rather than treating
missing flow as confirmation. Transaction costs are estimated; results are
exploratory and do not promise future performance.

For the first Order Flow experiment, run a 30-calendar-day baseline and fetch
SOXX time-and-sales around its candidate entries:

```sh
./build/daytrader orderflow-backtest 30
```

Use `ytd` to derive the calendar window from January 1 in the configured New
York time zone. Successful candidate windows are persisted immediately, so an
interrupted or pacing-limited run resumes from the existing cache:

```sh
./build/daytrader orderflow-backtest ytd
```

The classifier combines historical `TRADES` and `BID_ASK`, paging backward
until the complete requested five-minute evidence range is covered. It reports 30-second,
one-minute, and five-minute DeltaRatio, and distinguishes effective pressure
from absorption using the price response in SOXX ATR14 units. `ATRx` is the
completed-bar ATR5/ATR14 ratio, while the signed flow score combines 30-second
and one-minute Delta, Delta acceleration, and ATR response. The score is a
diagnostic, not a probability. In live monitoring, the QQQ/SOXX pressure state
affects TQQQ/SOXL entries while evidence quality is displayed rather than used
as a binary gate; historical Order Flow reports remain exploratory. Evidence
quality uses classification coverage, direct
quote-test coverage, window
completeness, and trade depth. Raw event samples are cached under
`data/ibkr/order_flow_ticks/`, so changes to the classifier can be replayed
without another IBKR request. An asterisk marks a horizon that the returned
sample did not fully cover.

## Runtime configuration

Supported environment variables:

- `DAYTRADER_IBKR_HOST`
- `DAYTRADER_IBKR_PORT`
- `DAYTRADER_IBKR_CLIENT_ID`
- `DAYTRADER_LIVE_CLIENT_ID`
- `DAYTRADER_BACKFILL_CLIENT_ID`
- `DAYTRADER_BAR_INTERVAL_SECONDS`
- `DAYTRADER_SOURCE_BAR_INTERVAL_SECONDS`
- `DAYTRADER_RECONNECT_DELAY_SECONDS`
- `DAYTRADER_MONITOR_DURATION`
- `DAYTRADER_MONITOR_LOOKBACK_DAYS`
- `DAYTRADER_DATA_TYPE`
- `DAYTRADER_DURATION`
- `DAYTRADER_BAR_SIZE`
- `DAYTRADER_HISTORICAL_DELAY_MINUTES`
- `DAYTRADER_TIME_ZONE`
- `DAYTRADER_MINUTE_DATA_DIR`

Example with a separate cache location:

```sh
DAYTRADER_MINUTE_DATA_DIR=/path/to/daytrader-data ./build/daytrader backtest 240
```

Strict compiler warnings are enabled by default and can be disabled with
`-DDAYTRADER_ENABLE_WARNINGS=OFF`.
